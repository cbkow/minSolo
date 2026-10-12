#include "engine.h"
#include "log.h"
#include <algorithm>

std::wstring Engine::iniPath() const { return dataDir() + L"\\settings.ini"; }

static std::set<std::wstring> readList(const std::wstring& ini, const wchar_t* section) {
    std::set<std::wstring> out;
    wchar_t buf[8192];
    DWORD n = GetPrivateProfileSectionW(section, buf, (DWORD)std::size(buf), ini.c_str());
    for (DWORD i = 0; i < n;) {
        std::wstring line = buf + i; i += (DWORD)line.size() + 1;
        size_t eq = line.find(L'=');
        if (eq != std::wstring::npos && line.substr(eq + 1) == L"1") out.insert(line.substr(0, eq));
    }
    return out;
}

static void writeList(const std::wstring& ini, const wchar_t* section, const std::set<std::wstring>& v) {
    WritePrivateProfileSectionW(section, L"", ini.c_str());   // clear
    for (auto& e : v) WritePrivateProfileStringW(section, e.c_str(), L"1", ini.c_str());
}

void Engine::load() {
    std::lock_guard<std::recursive_mutex> lk(mu_);
    auto ini = iniPath();
    rememberMutes = GetPrivateProfileIntW(L"settings", L"rememberMutes", 1, ini.c_str()) != 0;
    showAll = GetPrivateProfileIntW(L"settings", L"showAll", 0, ini.c_str()) != 0;
    soloMutesSystem = GetPrivateProfileIntW(L"settings", L"soloMutesSystem", 0, ini.c_str()) != 0;
    touched_ = readList(ini, L"touched");
    if (rememberMutes) muted = readList(ini, L"muted");
    wchar_t buf[8192];
    DWORD n = GetPrivateProfileSectionW(L"names", buf, (DWORD)std::size(buf), ini.c_str());
    for (DWORD i = 0; i < n;) {
        std::wstring line = buf + i; i += (DWORD)line.size() + 1;
        size_t eq = line.find(L'=');
        if (eq != std::wstring::npos) names[line.substr(0, eq)] = line.substr(eq + 1);
    }
    if (!muted.empty()) { std::wstring s; for (auto& m : muted) s += m + L" "; logLine(L"restored mutes: " + s); }
}

void Engine::save() { std::lock_guard<std::recursive_mutex> lk(mu_); saveLocked(); }

void Engine::saveLocked() {
    dirty_ = false;
    auto ini = iniPath();
    WritePrivateProfileStringW(L"settings", L"rememberMutes", rememberMutes ? L"1" : L"0", ini.c_str());
    WritePrivateProfileStringW(L"settings", L"showAll", showAll ? L"1" : L"0", ini.c_str());
    WritePrivateProfileStringW(L"settings", L"soloMutesSystem", soloMutesSystem ? L"1" : L"0", ini.c_str());
    writeList(ini, L"muted", rememberMutes ? muted : std::set<std::wstring>{});
    writeList(ini, L"touched", touched_);
    WritePrivateProfileSectionW(L"names", L"", ini.c_str());
    for (auto& m : muted) { auto it = names.find(m); if (it != names.end()) WritePrivateProfileStringW(L"names", m.c_str(), it->second.c_str(), ini.c_str()); }
}

bool Engine::shouldMute(const SessionInfo& s) const {
    if (s.systemSounds) return !soloed.empty() && soloMutesSystem;
    if (muted.count(s.exe)) return true;
    if (!soloed.empty()) return soloed.count(s.exe) == 0;
    return false;
}

void Engine::applyTo(SessionInfo& s) {
    std::lock_guard<std::recursive_mutex> lk(mu_);
    applyLocked(s);
    if (dirty_) saveLocked();
}

void Engine::applyLocked(SessionInfo& s) {
    if (s.state == AudioSessionStateExpired) return;
    bool want = shouldMute(s);
    // Any app we want muted is ours to unmute later — even if Windows already had it muted
    // (it remembers per-app mute across restarts, e.g. after a crash of a previous run).
    if (want && !s.systemSounds && touched_.insert(s.exe).second) dirty_ = true;
    if (want && !s.muted) {
        if (s.setMute(true)) { ours_.insert(s.instanceID); logLine(L"muted " + s.exe + L" pid " + std::to_wstring(s.pid) + L" on " + s.device); }
    } else if (!want && s.muted) {
        // Unmute only what we muted — or what a previous run left muted (Windows persists per-app mute).
        bool ours = ours_.count(s.instanceID) > 0 || touched_.count(s.exe) > 0;
        if (ours && s.setMute(false)) { ours_.erase(s.instanceID); logLine(L"unmuted " + s.exe + L" pid " + std::to_wstring(s.pid)); }
    }
    // A soloed app that was muted by hand stays as the user left it; solo only silences the others.
}

void Engine::apply(std::vector<SessionInfo>& sessions) {
    std::lock_guard<std::recursive_mutex> lk(mu_);
    for (auto& s : sessions) {
        if (!s.systemSounds && !s.exe.empty()) names[s.exe] = SessionWatcher::friendlyName(s.exe, s.exePath);
        applyLocked(s);
    }
    // Forget "touched" exes that are no longer muted and currently audible, so we stop claiming them.
    for (auto it = touched_.begin(); it != touched_.end();) {
        bool still = muted.count(*it) > 0;
        if (!still) {
            bool anyMuted = std::any_of(sessions.begin(), sessions.end(), [&](const SessionInfo& s) { return s.exe == *it && s.muted; });
            if (!anyMuted) { it = touched_.erase(it); dirty_ = true; continue; }
        }
        ++it;
    }
    if (dirty_) saveLocked();   // crash recovery depends on this list being on disk
}

void Engine::recoverAfterCrash(std::vector<SessionInfo>& sessions) {
    std::lock_guard<std::recursive_mutex> lk(mu_);
    if (touched_.empty()) return;
    int n = 0;
    for (auto& s : sessions)
        if (touched_.count(s.exe) && s.muted && !shouldMute(s)) { if (s.setMute(false)) ++n; }
    if (n) logLine(L"recovered " + std::to_wstring(n) + L" session(s) left muted by a previous run");
}

void Engine::releaseAll(std::vector<SessionInfo>& sessions) {
    std::lock_guard<std::recursive_mutex> lk(mu_);
    auto savedMuted = muted;
    muted.clear(); soloed.clear();
    apply(sessions);          // want=false everywhere → unmutes ours_/touched_ sessions
    ours_.clear(); touched_.clear();
    muted = savedMuted;       // the remembered list survives a clean quit
    saveLocked();
}

void Engine::setMuted(const std::wstring& exe, bool on) {
    std::lock_guard<std::recursive_mutex> lk(mu_);
    if (on) { muted.insert(exe); soloed.erase(exe); } else muted.erase(exe);
    save();
}
void Engine::setSoloed(const std::wstring& exe, bool on) {
    std::lock_guard<std::recursive_mutex> lk(mu_);
    if (on) { soloed.insert(exe); muted.erase(exe); } else soloed.erase(exe);
    save();
}
void Engine::clearAll() { std::lock_guard<std::recursive_mutex> lk(mu_); muted.clear(); soloed.clear(); save(); }

std::vector<AppEntry> Engine::apps(const std::vector<SessionInfo>& sessions) const {
    std::lock_guard<std::recursive_mutex> lk(mu_);
    std::map<std::wstring, AppEntry> m;
    for (auto& s : sessions) {
        if (s.systemSounds || s.state == AudioSessionStateExpired) continue;
        auto& e = m[s.exe];
        e.exe = s.exe;
        if (e.exePath.empty()) e.exePath = s.exePath;
        if (e.name.empty()) { auto it = names.find(s.exe); e.name = it != names.end() ? it->second : s.exe; }
        e.active = e.active || s.state == AudioSessionStateActive;
        e.sessions++;
    }
    std::vector<AppEntry> out;
    for (auto& [k, e] : m)
        if (e.active || showAll || muted.count(k) || soloed.count(k)) out.push_back(e);
    for (auto& k : muted) if (!m.count(k)) { AppEntry e; e.exe = k; auto it = names.find(k); e.name = it != names.end() ? it->second : k; out.push_back(e); }
    for (auto& k : soloed) if (!m.count(k)) { AppEntry e; e.exe = k; auto it = names.find(k); e.name = it != names.end() ? it->second : k; out.push_back(e); }
    std::sort(out.begin(), out.end(), [](const AppEntry& a, const AppEntry& b) { return _wcsicmp(a.name.c_str(), b.name.c_str()) < 0; });
    return out;
}
