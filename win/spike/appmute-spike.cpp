// appmute-spike: answer the open questions in plans/app-mute-plan-windows.md.
//
//   list                    every audio session on every active render endpoint
//   mute <exe> | unmute <exe>   ISimpleAudioVolume::SetMute on every session owned by <exe>
//   solo <exe> [--system]   mute every session NOT owned by <exe> (system sounds too with --system),
//                           keep muting new sessions as they appear, restore everything on Ctrl-C
//   hold <exe>              mute <exe>, watch for new sessions of it, restore on Ctrl-C
//
// Build (vcvars64): cl /nologo /EHsc /W3 /std:c++17 /DUNICODE /D_UNICODE appmute-spike.cpp ole32.lib
#define WIN32_LEAN_AND_MEAN
#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <mmdeviceapi.h>
#include <audiopolicy.h>
#include <functiondiscoverykeys_devpkey.h>
#include <psapi.h>
#include <atomic>
#include <cstdio>
#include <map>
#include <set>
#include <memory>
#include <string>
#include <vector>
#include <algorithm>
#include <functional>

#pragma comment(lib, "ole32.lib")

static const GUID kMyContext = {0x6d1e5a10, 0x2b2c, 0x4f1e, {0x9a, 0x7d, 0x51, 0x6e, 0x53, 0x6f, 0x6c, 0x6f}}; // "minSolo"
static std::atomic<bool> g_stop{false};

template <class T> struct Com {
    T* p = nullptr;
    Com() = default;
    Com(const Com&) = delete; Com& operator=(const Com&) = delete;
    ~Com() { if (p) p->Release(); }
    T** operator&() { return &p; }
    T* operator->() const { return p; }
    explicit operator bool() const { return p != nullptr; }
};

static std::wstring lower(std::wstring s) { std::transform(s.begin(), s.end(), s.begin(), ::towlower); return s; }

static std::wstring exeName(DWORD pid) {
    if (pid == 0) return L"(system)";
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!h) return L"pid " + std::to_wstring(pid);
    wchar_t buf[MAX_PATH * 2]; DWORD n = (DWORD)std::size(buf);
    std::wstring out = QueryFullProcessImageNameW(h, 0, buf, &n) ? std::wstring(buf) : L"pid " + std::to_wstring(pid);
    CloseHandle(h);
    size_t slash = out.find_last_of(L"\\/");
    return slash == std::wstring::npos ? out : out.substr(slash + 1);
}

static const wchar_t* stateName(AudioSessionState s) {
    switch (s) { case AudioSessionStateActive: return L"active"; case AudioSessionStateInactive: return L"inactive"; case AudioSessionStateExpired: return L"expired"; }
    return L"?";
}

struct Session {
    Com<IAudioSessionControl2> ctl;
    Com<ISimpleAudioVolume> vol;
    std::wstring device, exe, display, instanceID;
    DWORD pid = 0;
    bool systemSounds = false;
    AudioSessionState state = AudioSessionStateInactive;
    BOOL muted = FALSE;
};

static bool wrap(IAudioSessionControl* raw, const std::wstring& device, Session& out) {
    if (FAILED(raw->QueryInterface(__uuidof(IAudioSessionControl2), (void**)&out.ctl))) return false;
    if (FAILED(raw->QueryInterface(__uuidof(ISimpleAudioVolume), (void**)&out.vol))) return false;
    out.device = device;
    out.ctl->GetProcessId(&out.pid);
    out.systemSounds = out.ctl->IsSystemSoundsSession() == S_OK;
    out.exe = out.systemSounds ? L"(system sounds)" : exeName(out.pid);
    LPWSTR name = nullptr;
    if (SUCCEEDED(out.ctl->GetDisplayName(&name)) && name) { out.display = name; CoTaskMemFree(name); }
    LPWSTR inst = nullptr;
    if (SUCCEEDED(out.ctl->GetSessionInstanceIdentifier(&inst)) && inst) { out.instanceID = inst; CoTaskMemFree(inst); }
    out.ctl->GetState(&out.state);
    out.vol->GetMute(&out.muted);
    return true;
}

// Every session on every active render endpoint. Keeps the IAudioSessionManager2s alive
// (needed for notifications) in `managers`.
struct Enumerated {
    std::vector<std::shared_ptr<Session>> sessions;
    std::vector<std::pair<std::wstring, Com<IAudioSessionManager2>*>> managers;
};

static HRESULT enumerate(std::vector<std::shared_ptr<Session>>& sessions,
                         std::vector<std::pair<std::wstring, std::shared_ptr<Com<IAudioSessionManager2>>>>* managersOut) {
    Com<IMMDeviceEnumerator> en;
    HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator), (void**)&en);
    if (FAILED(hr)) { fwprintf(stderr, L"MMDeviceEnumerator failed 0x%08lx\n", hr); return hr; }
    Com<IMMDeviceCollection> devs;
    hr = en->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &devs);
    if (FAILED(hr)) { fwprintf(stderr, L"EnumAudioEndpoints failed 0x%08lx\n", hr); return hr; }
    UINT n = 0; devs->GetCount(&n);
    for (UINT i = 0; i < n; ++i) {
        Com<IMMDevice> dev; if (FAILED(devs->Item(i, &dev))) continue;
        std::wstring devName = L"?";
        Com<IPropertyStore> props;
        if (SUCCEEDED(dev->OpenPropertyStore(STGM_READ, &props))) {
            PROPVARIANT v; PropVariantInit(&v);
            if (SUCCEEDED(props->GetValue(PKEY_Device_FriendlyName, &v)) && v.vt == VT_LPWSTR) devName = v.pwszVal;
            PropVariantClear(&v);
        }
        auto mgr = std::make_shared<Com<IAudioSessionManager2>>();
        if (FAILED(dev->Activate(__uuidof(IAudioSessionManager2), CLSCTX_ALL, nullptr, (void**)&mgr->p))) continue;
        Com<IAudioSessionEnumerator> se;
        if (FAILED((*mgr)->GetSessionEnumerator(&se))) continue;   // documented: call once before notifications
        int count = 0; se->GetCount(&count);
        for (int j = 0; j < count; ++j) {
            Com<IAudioSessionControl> raw; if (FAILED(se->GetSession(j, &raw))) continue;
            auto s = std::make_shared<Session>();
            if (wrap(raw.p, devName, *s)) sessions.push_back(s);
        }
        if (managersOut) managersOut->push_back({devName, mgr});
    }
    return S_OK;
}

static void printSessions(const std::vector<std::shared_ptr<Session>>& ss) {
    wprintf(L"%-7s %-8s %-6s %-36s %s\n", L"pid", L"state", L"muted", L"exe", L"device / display name");
    for (auto& s : ss)
        wprintf(L"%-7lu %-8s %-6s %-36s %s%s%s\n", s->pid, stateName(s->state), s->muted ? L"YES" : L"-",
                s->exe.c_str(), s->device.c_str(), s->display.empty() ? L"" : L" / ", s->display.c_str());
}

static bool owned(const Session& s, const std::wstring& exe) { return !s.systemSounds && lower(s.exe) == lower(exe); }

// Session-created callback: while a solo/hold is active, new sessions get the policy immediately.
struct Notifier : IAudioSessionNotification {
    LONG ref = 1;
    std::function<void(IAudioSessionControl*)> onNew;
    STDMETHODIMP QueryInterface(REFIID iid, void** pp) override {
        if (iid == __uuidof(IUnknown) || iid == __uuidof(IAudioSessionNotification)) { *pp = this; AddRef(); return S_OK; }
        *pp = nullptr; return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return InterlockedIncrement(&ref); }
    STDMETHODIMP_(ULONG) Release() override { LONG r = InterlockedDecrement(&ref); if (!r) delete this; return r; }
    STDMETHODIMP OnSessionCreated(IAudioSessionControl* s) override { if (onNew) onNew(s); return S_OK; }
};

static BOOL WINAPI ctrlHandler(DWORD) { g_stop = true; return TRUE; }

int wmain(int argc, wchar_t** argv) {
    if (argc < 2) { wprintf(L"usage: appmute-spike list | mute <exe> | unmute <exe> | solo <exe> [--system] | hold <exe>\n"); return 2; }
    // MTA: documented requirement for IAudioSessionNotification callbacks.
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(hr)) { fwprintf(stderr, L"CoInitializeEx 0x%08lx\n", hr); return 1; }
    std::wstring cmd = argv[1];
    std::wstring exe = argc > 2 ? argv[2] : L"";
    bool muteSystem = argc > 3 && std::wstring(argv[3]) == L"--system";

    std::vector<std::shared_ptr<Session>> sessions;
    std::vector<std::pair<std::wstring, std::shared_ptr<Com<IAudioSessionManager2>>>> managers;
    if (FAILED(enumerate(sessions, &managers))) return 1;

    if (cmd == L"list") { printSessions(sessions); wprintf(L"%zu session(s) on %zu device(s)\n", sessions.size(), managers.size()); return 0; }
    if (exe.empty()) { wprintf(L"need an exe name, e.g. chrome.exe\n"); return 2; }

    if (cmd == L"mute" || cmd == L"unmute") {
        BOOL m = cmd == L"mute";
        int n = 0;
        for (auto& s : sessions) if (owned(*s, exe)) { hr = s->vol->SetMute(m, &kMyContext); wprintf(L"%s pid %lu on %s: 0x%08lx\n", cmd.c_str(), s->pid, s->device.c_str(), hr); ++n; }
        wprintf(L"%d session(s) changed (Windows remembers this across restarts)\n", n);
        return n ? 0 : 1;
    }

    bool solo = cmd == L"solo";
    if (!solo && cmd != L"hold") { wprintf(L"unknown command\n"); return 2; }

    // Remember original mute states so Ctrl-C restores them.
    auto shouldMute = [&](const Session& s) {
        if (solo) return s.systemSounds ? muteSystem : !owned(s, exe);
        return owned(s, exe);
    };
    std::vector<std::pair<std::shared_ptr<Session>, BOOL>> changed;
    for (auto& s : sessions) {
        if (s->state == AudioSessionStateExpired) continue;
        bool want = shouldMute(*s);
        if (solo && owned(*s, exe) && s->muted) { changed.push_back({s, s->muted}); s->vol->SetMute(FALSE, &kMyContext); wprintf(L"unmute (solo target) %s\n", s->exe.c_str()); }
        else if (want && !s->muted) { changed.push_back({s, s->muted}); s->vol->SetMute(TRUE, &kMyContext); wprintf(L"mute %s (pid %lu, %s)\n", s->exe.c_str(), s->pid, s->device.c_str()); }
    }

    // New sessions while we hold.
    std::vector<Notifier*> notifiers;
    std::set<std::wstring> seenInstances;
    for (auto& s : sessions) seenInstances.insert(s->instanceID);
    for (auto& [devName, mgr] : managers) {
        auto* nf = new Notifier();
        nf->onNew = [&, devName](IAudioSessionControl* raw) {
            auto s = std::make_shared<Session>();
            if (!wrap(raw, devName, *s)) return;
            // OnSessionCreated fired twice per new session in testing: de-dupe on the instance id.
            if (!seenInstances.insert(s->instanceID).second) { wprintf(L"[new session] duplicate callback for pid %lu ignored\n", s->pid); return; }
            if (shouldMute(*s) && !s->muted) { s->vol->SetMute(TRUE, &kMyContext); wprintf(L"[new session] muted %s (pid %lu)\n", s->exe.c_str(), s->pid); changed.push_back({s, FALSE}); }
            else wprintf(L"[new session] %s (pid %lu) left alone\n", s->exe.c_str(), s->pid);
        };
        hr = (*mgr)->RegisterSessionNotification(nf);
        wprintf(L"RegisterSessionNotification on %s: 0x%08lx\n", devName.c_str(), hr);
        notifiers.push_back(nf);
    }

    SetConsoleCtrlHandler(ctrlHandler, TRUE);
    wprintf(L"holding (%zu session(s) muted). Ctrl-C restores.\n", changed.size());
    fflush(stdout);
    int secs = 0;
    const wchar_t* env = _wgetenv(L"SPIKE_SECONDS");
    int limit = env ? _wtoi(env) : 0;
    while (!g_stop) { Sleep(1000); if (limit && ++secs >= limit) break; }

    for (size_t i = 0; i < managers.size(); ++i) { (*managers[i].second)->UnregisterSessionNotification(notifiers[i]); notifiers[i]->Release(); }
    for (auto& [s, was] : changed) { s->vol->SetMute(was, &kMyContext); }
    wprintf(L"restored %zu session(s)\n", changed.size());
    return 0;
}
