#include "sessions.h"
#include "log.h"
#include <functiondiscoverykeys_devpkey.h>
#include <tlhelp32.h>
#include <algorithm>
#include <map>
#include <set>
#pragma comment(lib, "version.lib")

// {6D1E5A10-2B2C-4F1E-9A7D-516E536F6C6F} "minSolo"
static const GUID kContext = {0x6d1e5a10, 0x2b2c, 0x4f1e, {0x9a, 0x7d, 0x51, 0x6e, 0x53, 0x6f, 0x6c, 0x6f}};
const GUID& SessionWatcher::eventContext() { return kContext; }

static std::wstring lower(std::wstring s) { std::transform(s.begin(), s.end(), s.begin(), ::towlower); return s; }

bool SessionInfo::setMute(bool on) {
    if (!vol) return false;
    HRESULT hr = vol->SetMute(on ? TRUE : FALSE, &kContext);
    if (SUCCEEDED(hr)) muted = on;
    return SUCCEEDED(hr);
}

// Full image path when the process can be opened; otherwise just the exe name from a
// Toolhelp snapshot, which works for processes at a higher integrity level or in another session.
static std::wstring imagePath(DWORD pid) {
    if (pid == 0) return L"";
    if (HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid)) {
        wchar_t buf[MAX_PATH * 2]; DWORD n = (DWORD)std::size(buf);
        std::wstring out = QueryFullProcessImageNameW(h, 0, buf, &n) ? std::wstring(buf) : L"";
        CloseHandle(h);
        if (!out.empty()) return out;
    }
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return L"";
    PROCESSENTRY32W pe{}; pe.dwSize = sizeof(pe);
    std::wstring out;
    if (Process32FirstW(snap, &pe)) do { if (pe.th32ProcessID == pid) { out = pe.szExeFile; break; } } while (Process32NextW(snap, &pe));
    CloseHandle(snap);
    return out;
}

std::wstring SessionWatcher::friendlyName(const std::wstring& exe, const std::wstring& exePath) {
    static std::map<std::wstring, std::wstring> cache;
    auto it = cache.find(exePath);
    if (it != cache.end()) return it->second;
    std::wstring name = exe;
    size_t dot = name.rfind(L'.');
    if (dot != std::wstring::npos) name = name.substr(0, dot);
    if (!name.empty()) name[0] = (wchar_t)towupper(name[0]);
    if (!exePath.empty()) {
        DWORD dummy = 0, size = GetFileVersionInfoSizeW(exePath.c_str(), &dummy);
        if (size) {
            std::vector<BYTE> data(size);
            if (GetFileVersionInfoW(exePath.c_str(), 0, size, data.data())) {
                struct { WORD lang, cp; }* tr = nullptr; UINT trLen = 0;
                if (VerQueryValueW(data.data(), L"\\VarFileInfo\\Translation", (void**)&tr, &trLen) && trLen >= 4) {
                    wchar_t key[64];
                    swprintf(key, 64, L"\\StringFileInfo\\%04x%04x\\FileDescription", tr[0].lang, tr[0].cp);
                    wchar_t* desc = nullptr; UINT dl = 0;
                    if (VerQueryValueW(data.data(), key, (void**)&desc, &dl) && desc && dl > 1 && wcslen(desc) > 0) name = desc;
                }
            }
        }
    }
    cache[exePath] = name;
    return name;
}

static bool wrap(IAudioSessionControl* raw, const std::wstring& device, SessionInfo& out) {
    if (FAILED(raw->QueryInterface(__uuidof(IAudioSessionControl2), (void**)&out.ctl))) return false;
    if (FAILED(raw->QueryInterface(__uuidof(ISimpleAudioVolume), (void**)&out.vol))) return false;
    out.device = device;
    out.ctl->GetProcessId(&out.pid);
    out.systemSounds = out.ctl->IsSystemSoundsSession() == S_OK;
    if (out.systemSounds) { out.exe = L"(system sounds)"; }
    else {
        out.exePath = imagePath(out.pid);
        size_t slash = out.exePath.find_last_of(L"\\/");
        out.exe = lower(slash == std::wstring::npos ? out.exePath : out.exePath.substr(slash + 1));
        if (out.exe.empty()) out.exe = L"pid " + std::to_wstring(out.pid);
    }
    LPWSTR inst = nullptr;
    if (SUCCEEDED(out.ctl->GetSessionInstanceIdentifier(&inst)) && inst) { out.instanceID = inst; CoTaskMemFree(inst); }
    out.ctl->GetState(&out.state);
    BOOL m = FALSE; out.vol->GetMute(&m); out.muted = m != FALSE;
    return true;
}

struct SessionWatcher::Impl;

// One per endpoint, so a created session is reported with its device name.
struct DeviceNotifier : IAudioSessionNotification {
    LONG ref = 1;
    SessionWatcher::Impl* owner;
    std::wstring device;
    Com<IAudioSessionManager2> mgr;
    DeviceNotifier(SessionWatcher::Impl* o, std::wstring d, Com<IAudioSessionManager2> m) : owner(o), device(std::move(d)), mgr(std::move(m)) {}
    STDMETHODIMP QueryInterface(REFIID iid, void** pp) override {
        if (iid == __uuidof(IUnknown) || iid == __uuidof(IAudioSessionNotification)) { *pp = this; AddRef(); return S_OK; }
        *pp = nullptr; return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return InterlockedIncrement(&ref); }
    STDMETHODIMP_(ULONG) Release() override { LONG r = InterlockedDecrement(&ref); if (!r) delete this; return r; }
    STDMETHODIMP OnSessionCreated(IAudioSessionControl* raw) override;
};

struct SessionWatcher::Impl : IMMNotificationClient {
    LONG ref = 1;
    std::mutex mu;
    Com<IMMDeviceEnumerator> enumerator;
    std::vector<DeviceNotifier*> notifiers;   // one per active render endpoint; each keeps its manager alive
    std::set<std::wstring> seenInstances;
    NewSessionFn onNew;
    EndpointsChangedFn onEndpoints;

    // IUnknown
    STDMETHODIMP QueryInterface(REFIID iid, void** pp) override {
        if (iid == __uuidof(IUnknown) || iid == __uuidof(IMMNotificationClient)) { *pp = static_cast<IMMNotificationClient*>(this); AddRef(); return S_OK; }
        *pp = nullptr; return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return InterlockedIncrement(&ref); }
    STDMETHODIMP_(ULONG) Release() override { LONG r = InterlockedDecrement(&ref); return r; }  // lifetime owned by shared_ptr

    // IMMNotificationClient: any endpoint change → caller rescans + refreshes.
    STDMETHODIMP OnDeviceStateChanged(LPCWSTR, DWORD) override { if (onEndpoints) onEndpoints(); return S_OK; }
    STDMETHODIMP OnDeviceAdded(LPCWSTR) override { if (onEndpoints) onEndpoints(); return S_OK; }
    STDMETHODIMP OnDeviceRemoved(LPCWSTR) override { if (onEndpoints) onEndpoints(); return S_OK; }
    STDMETHODIMP OnDefaultDeviceChanged(EDataFlow flow, ERole role, LPCWSTR) override {
        if (flow == eRender && role == eConsole && onEndpoints) onEndpoints();
        return S_OK;
    }
    STDMETHODIMP OnPropertyValueChanged(LPCWSTR, const PROPERTYKEY) override { return S_OK; }

    void sessionCreated(IAudioSessionControl* raw, const std::wstring& device) {
        SessionInfo s;
        if (!wrap(raw, device, s)) return;
        {
            std::lock_guard<std::mutex> lk(mu);
            if (!seenInstances.insert(s.instanceID).second) return;   // fires twice per session sometimes
        }
        if (onNew) onNew(s);
    }

    std::wstring deviceName(IMMDevice* dev) {
        std::wstring name = L"?";
        Com<IPropertyStore> props;
        if (SUCCEEDED(dev->OpenPropertyStore(STGM_READ, &props))) {
            PROPVARIANT v; PropVariantInit(&v);
            if (SUCCEEDED(props->GetValue(PKEY_Device_FriendlyName, &v)) && v.vt == VT_LPWSTR) name = v.pwszVal;
            PropVariantClear(&v);
        }
        return name;
    }

    void unregisterAll() {
        for (auto* n : notifiers) { n->mgr->UnregisterSessionNotification(n); n->Release(); }
        notifiers.clear();
    }

    void registerManagers() {
        std::lock_guard<std::mutex> lk(mu);
        unregisterAll();
        Com<IMMDeviceCollection> devs;
        if (FAILED(enumerator->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &devs))) return;
        UINT n = 0; devs->GetCount(&n);
        for (UINT i = 0; i < n; ++i) {
            Com<IMMDevice> dev; if (FAILED(devs->Item(i, &dev))) continue;
            Com<IAudioSessionManager2> mgr;
            if (FAILED(dev->Activate(__uuidof(IAudioSessionManager2), CLSCTX_ALL, nullptr, (void**)&mgr))) continue;
            Com<IAudioSessionEnumerator> se;
            mgr->GetSessionEnumerator(&se);   // documented: once, before notifications arrive
            auto* dn = new DeviceNotifier(this, deviceName(dev.p), mgr);
            mgr->RegisterSessionNotification(dn);
            notifiers.push_back(dn);
        }
        logLine(L"watching " + std::to_wstring(notifiers.size()) + L" render endpoint(s)");
    }

    std::vector<SessionInfo> snapshot() {
        std::vector<SessionInfo> out;
        Com<IMMDeviceCollection> devs;
        if (!enumerator || FAILED(enumerator->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &devs))) return out;
        UINT n = 0; devs->GetCount(&n);
        for (UINT i = 0; i < n; ++i) {
            Com<IMMDevice> dev; if (FAILED(devs->Item(i, &dev))) continue;
            std::wstring name = deviceName(dev.p);
            Com<IAudioSessionManager2> mgr;
            if (FAILED(dev->Activate(__uuidof(IAudioSessionManager2), CLSCTX_ALL, nullptr, (void**)&mgr))) continue;
            Com<IAudioSessionEnumerator> se;
            if (FAILED(mgr->GetSessionEnumerator(&se))) continue;
            int count = 0; se->GetCount(&count);
            for (int j = 0; j < count; ++j) {
                Com<IAudioSessionControl> raw; if (FAILED(se->GetSession(j, &raw))) continue;
                SessionInfo s;
                if (wrap(raw.p, name, s)) {
                    { std::lock_guard<std::mutex> lk(mu); seenInstances.insert(s.instanceID); }
                    out.push_back(std::move(s));
                }
            }
        }
        return out;
    }
};

STDMETHODIMP DeviceNotifier::OnSessionCreated(IAudioSessionControl* raw) { owner->sessionCreated(raw, device); return S_OK; }

bool SessionWatcher::start(NewSessionFn onNew, EndpointsChangedFn onEndpoints) {
    impl_ = std::make_shared<Impl>();
    impl_->onNew = std::move(onNew);
    impl_->onEndpoints = std::move(onEndpoints);
    HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator), (void**)&impl_->enumerator);
    if (FAILED(hr)) { logLine(L"MMDeviceEnumerator failed"); return false; }
    impl_->enumerator->RegisterEndpointNotificationCallback(impl_.get());
    impl_->registerManagers();
    return true;
}

void SessionWatcher::stop() {
    if (!impl_) return;
    if (impl_->enumerator) impl_->enumerator->UnregisterEndpointNotificationCallback(impl_.get());
    std::lock_guard<std::mutex> lk(impl_->mu);
    impl_->unregisterAll();
}

void SessionWatcher::rescanEndpoints() { if (impl_) impl_->registerManagers(); }
std::vector<SessionInfo> SessionWatcher::snapshot() { return impl_ ? impl_->snapshot() : std::vector<SessionInfo>{}; }
