#pragma once
#include <windows.h>
#include <mmdeviceapi.h>
#include <audiopolicy.h>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

template <class T> struct Com {
    T* p = nullptr;
    Com() = default;
    Com(const Com& o) : p(o.p) { if (p) p->AddRef(); }
    Com& operator=(const Com& o) { if (o.p) o.p->AddRef(); if (p) p->Release(); p = o.p; return *this; }
    ~Com() { if (p) p->Release(); }
    T** operator&() { return &p; }
    T* operator->() const { return p; }
    explicit operator bool() const { return p != nullptr; }
};

/// One audio session on one render endpoint.
struct SessionInfo {
    Com<IAudioSessionControl2> ctl;
    Com<ISimpleAudioVolume> vol;
    std::wstring device;        // endpoint friendly name
    std::wstring exe;           // lower-case basename, e.g. "chrome.exe"; "(system sounds)" for the system session
    std::wstring exePath;       // full image path
    std::wstring instanceID;    // IAudioSessionControl2::GetSessionInstanceIdentifier
    DWORD pid = 0;
    bool systemSounds = false;
    AudioSessionState state = AudioSessionStateInactive;
    bool muted = false;

    bool setMute(bool on);      // tagged with minSolo's event context
};

/// Enumerates sessions and watches for new sessions and endpoint changes.
/// Callbacks arrive on COM worker threads; the caller marshals to its UI thread.
class SessionWatcher {
public:
    using NewSessionFn = std::function<void(SessionInfo&)>;
    using EndpointsChangedFn = std::function<void()>;

    bool start(NewSessionFn onNew, EndpointsChangedFn onEndpoints);
    void stop();
    std::vector<SessionInfo> snapshot();   // fresh enumeration of every active render endpoint
    void rescanEndpoints();                // re-register session notifications after device changes

    static const GUID& eventContext();
    static std::wstring friendlyName(const std::wstring& exe, const std::wstring& exePath);  // FileDescription or exe stem

    struct Impl;   // public so the per-endpoint notifier can call back into it
private:
    std::shared_ptr<Impl> impl_;
};
