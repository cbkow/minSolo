// minSolo for Windows: tray app that mutes or solos individual apps' audio sessions.
//
//   minSolo.exe                       run (single instance; a second launch just focuses nothing and exits)
//   minSolo.exe --mute <exe>          send a command to the running instance (or apply at startup):
//              --unmute <exe> | --solo <exe> | --unsolo <exe> | --clear | --quit | --show (open the flyout)
#include <windows.h>
#include <shellapi.h>
#include <shlwapi.h>
#include <appmodel.h>
#include <string>
#include <vector>
#include <windowsx.h>
#include "app.h"
#include "engine.h"
#include "flyout.h"
#include "log.h"
#include "resource.h"
#include "sessions.h"
#include "version.h"

static const wchar_t* kClass = L"minSoloTray";
static const UINT WM_TRAY = WM_APP + 1;
static const UINT WM_REFRESH = WM_APP + 2;     // endpoints changed / new session: refresh on the UI thread
static const UINT kTimer = 1;
enum : UINT { ID_CLEAR = 1, ID_SHOWALL, ID_REMEMBER, ID_SOLOSYS, ID_STARTUP, ID_QUIT, ID_ABOUT };

HWND g_hwnd;
SessionWatcher g_watcher;
Engine g_engine;
static UINT g_taskbarCreated;

void refresh() {
    auto sessions = g_watcher.snapshot();
    g_engine.apply(sessions);
}

static bool packaged() {
    // Packaged (MSIX) processes have a package identity; GetCurrentPackageFullName returns APPMODEL_ERROR_NO_PACKAGE otherwise.
    UINT32 n = 0;
    return GetCurrentPackageFullName(&n, nullptr) != APPMODEL_ERROR_NO_PACKAGE;
}

// Launch at login, unpackaged: HKCU\...\Run. Packaged: the MSIX startupTask, toggled in Settings > Apps > Startup.
static bool startupEnabled() {
    HKEY k; if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_READ, &k) != ERROR_SUCCESS) return false;
    DWORD type = 0, size = 0; bool on = RegQueryValueExW(k, L"minSolo", nullptr, &type, nullptr, &size) == ERROR_SUCCESS; RegCloseKey(k); return on;
}
static void setStartup(bool on) {
    HKEY k; if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, nullptr, 0, KEY_WRITE, nullptr, &k, nullptr) != ERROR_SUCCESS) return;
    if (on) { wchar_t path[MAX_PATH]; GetModuleFileNameW(nullptr, path, MAX_PATH); std::wstring v = L"\"" + std::wstring(path) + L"\""; RegSetValueExW(k, L"minSolo", 0, REG_SZ, (const BYTE*)v.c_str(), (DWORD)((v.size() + 1) * sizeof(wchar_t))); }
    else RegDeleteValueW(k, L"minSolo");
    RegCloseKey(k);
}

static void trayAdd() {
    NOTIFYICONDATAW nid{}; nid.cbSize = sizeof(nid); nid.hWnd = g_hwnd; nid.uID = 1;
    nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP; nid.uCallbackMessage = WM_TRAY;
    nid.hIcon = (HICON)LoadImageW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDI_TRAY), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_DEFAULTCOLOR);
    wcscpy_s(nid.szTip, L"minSolo");
    Shell_NotifyIconW(NIM_ADD, &nid);
    nid.uVersion = NOTIFYICON_VERSION_4; Shell_NotifyIconW(NIM_SETVERSION, &nid);
}
static void trayRemove() { NOTIFYICONDATAW nid{}; nid.cbSize = sizeof(nid); nid.hWnd = g_hwnd; nid.uID = 1; Shell_NotifyIconW(NIM_DELETE, &nid); }

void quitApp() { PostMessageW(g_hwnd, WM_CLOSE, 0, 0); }

static void settingsCommand(UINT id) {
    switch (id) {
    case ID_CLEAR: g_engine.clearAll(); break;
    case ID_SHOWALL: g_engine.showAll = !g_engine.showAll; g_engine.save(); break;
    case ID_REMEMBER: g_engine.rememberMutes = !g_engine.rememberMutes; g_engine.save(); break;
    case ID_SOLOSYS: g_engine.soloMutesSystem = !g_engine.soloMutesSystem; g_engine.save(); break;
    case ID_STARTUP: setStartup(!startupEnabled()); break;
    case ID_QUIT: quitApp(); return;
    default: return;
    }
    refresh();
}

// The gear menu. Owned by the flyout so opening it does not dismiss the flyout.
void showSettingsMenu(HWND owner, POINT pt) {
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING | (g_engine.showAll ? MF_CHECKED : 0), ID_SHOWALL, L"Show all audio apps");
    AppendMenuW(menu, MF_STRING | (g_engine.rememberMutes ? MF_CHECKED : 0), ID_REMEMBER, L"Remember mutes");
    AppendMenuW(menu, MF_STRING | (g_engine.soloMutesSystem ? MF_CHECKED : 0), ID_SOLOSYS, L"Solo also mutes system sounds");
    if (!packaged()) AppendMenuW(menu, MF_STRING | (startupEnabled() ? MF_CHECKED : 0), ID_STARTUP, L"Launch at login");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING | MF_GRAYED, ID_ABOUT, (L"minSolo " MINSOLO_VERSION_WSTR));
    AppendMenuW(menu, MF_STRING, ID_QUIT, L"Quit minSolo");
    UINT id = (UINT)TrackPopupMenuEx(menu, TPM_LEFTALIGN | TPM_BOTTOMALIGN | TPM_RETURNCMD | TPM_NONOTIFY, pt.x, pt.y, owner, nullptr);
    DestroyMenu(menu);
    if (id) settingsCommand(id);
}

// "--mute chrome.exe" etc., from the command line of this or another instance.
static bool applyCommand(const std::wstring& verb, const std::wstring& exe) {
    std::wstring e = exe; for (auto& c : e) c = (wchar_t)towlower(c);
    if (verb == L"--mute") g_engine.setMuted(e, true);
    else if (verb == L"--unmute") g_engine.setMuted(e, false);
    else if (verb == L"--solo") g_engine.setSoloed(e, true);
    else if (verb == L"--unsolo") g_engine.setSoloed(e, false);
    else if (verb == L"--clear") g_engine.clearAll();
    else if (verb == L"--quit") { quitApp(); return true; }
    else if (verb == L"--show") { flyoutToggle(); return true; }
    else return false;
    logLine(L"command " + verb + L" " + e);
    refresh();
    return true;
}

static LRESULT CALLBACK wndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_TRAY:
        switch (LOWORD(l)) {
        case WM_LBUTTONUP: case WM_CONTEXTMENU: case NIN_KEYSELECT: flyoutToggle(); break;
        }
        return 0;
    case WM_TIMER: refresh(); return 0;
    case WM_REFRESH: refresh(); return 0;
    case WM_COPYDATA: {
        auto* cds = (COPYDATASTRUCT*)l;
        std::wstring s((const wchar_t*)cds->lpData, cds->cbData / sizeof(wchar_t));
        size_t sp = s.find(L' ');
        applyCommand(s.substr(0, sp), sp == std::wstring::npos ? L"" : s.substr(sp + 1));
        return 1;
    }
    case WM_CLOSE: DestroyWindow(h); return 0;
    case WM_DESTROY: trayRemove(); PostQuitMessage(0); return 0;
    case WM_ENDSESSION: if (w) { g_engine.clearAll(); refresh(); } return 0;
    default:
        if (m == g_taskbarCreated) { trayAdd(); return 0; }
    }
    return DefWindowProcW(h, m, w, l);
}

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR, int) {
    logInit();
    int argc = 0; LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    std::wstring verb = argc > 1 ? argv[1] : L"", arg = argc > 2 ? argv[2] : L"";

    // Single instance: forward a command to the running one, or exit.
    HANDLE mutex = CreateMutexW(nullptr, TRUE, L"Local\\minSolo.single");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        if (HWND other = FindWindowW(kClass, nullptr)) {
            if (!verb.empty()) {
                std::wstring msg = verb + (arg.empty() ? L"" : L" " + arg);
                COPYDATASTRUCT cds{0, (DWORD)((msg.size()) * sizeof(wchar_t)), (void*)msg.c_str()};
                SendMessageW(other, WM_COPYDATA, 0, (LPARAM)&cds);
            }
        }
        return 0;
    }

    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);   // MTA: required for session notifications
    if (FAILED(hr)) return 1;

    WNDCLASSW wc{}; wc.lpfnWndProc = wndProc; wc.hInstance = inst; wc.lpszClassName = kClass;
    RegisterClassW(&wc);
    g_taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
    g_hwnd = CreateWindowExW(0, kClass, L"minSolo", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, inst, nullptr);

    flyoutInit(inst);
    g_engine.load();
    g_watcher.start(
        [](SessionInfo& s) { g_engine.applyTo(s); PostMessageW(g_hwnd, WM_REFRESH, 0, 0); },   // worker thread: act at once
        []() { g_watcher.rescanEndpoints(); PostMessageW(g_hwnd, WM_REFRESH, 0, 0); });
    {
        auto sessions = g_watcher.snapshot();
        g_engine.recoverAfterCrash(sessions);
        g_engine.apply(sessions);
    }
    if (!verb.empty()) applyCommand(verb, arg);
    trayAdd();
    SetTimer(g_hwnd, kTimer, 1000, nullptr);
    logLine(L"minSolo " MINSOLO_VERSION_WSTR L" started" + std::wstring(packaged() ? L" (packaged)" : L""));

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) { TranslateMessage(&msg); DispatchMessageW(&msg); }

    // Quit = release everything; a muted app must not stay muted after minSolo is gone.
    { auto sessions = g_watcher.snapshot(); g_engine.releaseAll(sessions); }
    g_watcher.stop();
    logLine(L"stopped");
    CoUninitialize();
    ReleaseMutex(mutex); CloseHandle(mutex);
    return 0;
}
