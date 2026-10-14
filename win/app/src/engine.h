#pragma once
#include "sessions.h"
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <vector>

/// An app as shown in the menu: every session with the same exe basename, on every endpoint.
struct AppEntry {
    std::wstring exe;        // key
    std::wstring name;       // friendly name
    std::wstring exePath;    // for the icon
    bool active = false;     // any session active (playing)
    int sessions = 0;
    bool operator==(const AppEntry& o) const { return exe == o.exe && name == o.name && active == o.active && sessions == o.sessions; }
};

/// Mute/solo state, applied to live sessions. Only sessions minSolo muted are ever unmuted by it;
/// what the user mutes in the Volume Mixer is left alone.
class Engine {
public:
    std::set<std::wstring> muted, soloed;
    bool soloMutesSystem = false;
    bool rememberMutes = true;
    bool showAll = false;

    void load();                       // settings + crash-recovery list from %LOCALAPPDATA%\minSolo\settings.ini
    void save();

    void apply(std::vector<SessionInfo>& sessions);   // reconcile every session with the state
    void applyTo(SessionInfo& s);                     // one session (new-session callback)
    void recoverAfterCrash(std::vector<SessionInfo>& sessions);  // unmute what a previous run left muted
    void releaseAll(std::vector<SessionInfo>& sessions);         // on quit: unmute everything we muted, keep the remembered list

    void setMuted(const std::wstring& exe, bool on);
    void setSoloed(const std::wstring& exe, bool on);
    void clearAll();
    bool isMuted(const std::wstring& exe) const { return muted.count(exe) > 0; }
    bool isSoloed(const std::wstring& exe) const { return soloed.count(exe) > 0; }

    std::vector<AppEntry> apps(const std::vector<SessionInfo>& sessions) const;   // grouped, filtered for the menu
    std::map<std::wstring, std::wstring> names;   // exe → friendly name, remembered for not-running apps

private:
    bool shouldMute(const SessionInfo& s) const;
    void applyLocked(SessionInfo& s);
    void saveLocked();
    mutable std::recursive_mutex mu_;   // everything runs on the UI thread now; kept so a stray caller stays safe
    bool dirty_ = false;                // touched_ changed since the last save
    std::set<std::wstring> touched_;   // exes minSolo has muted (Windows remembers per-app mute; see recoverAfterCrash)
    std::set<std::wstring> ours_;      // session instance IDs minSolo muted in this run
    std::wstring iniPath() const;
};
