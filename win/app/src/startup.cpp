#include "startup.h"
#include <windows.h>
#include <appmodel.h>
#include <shellapi.h>
#include <string>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.ApplicationModel.h>
#include "log.h"

using winrt::Windows::ApplicationModel::StartupTask;
using winrt::Windows::ApplicationModel::StartupTaskState;

static const wchar_t* kRunKey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
static const wchar_t* kTaskId = L"minSoloStartup";   // = StartupTask TaskId in msix/AppxManifest.xml.in

bool packaged() {
    // GetCurrentPackageFullName returns APPMODEL_ERROR_NO_PACKAGE without package identity.
    UINT32 n = 0;
    return GetCurrentPackageFullName(&n, nullptr) != APPMODEL_ERROR_NO_PACKAGE;
}

// The calls below block the UI thread for a few ms; it is an MTA thread (main.cpp), where .get() is allowed.
static StartupTaskState taskState() {
    try { return StartupTask::GetAsync(kTaskId).get().State(); }
    catch (const winrt::hresult_error& e) { logLine(L"StartupTask: " + std::wstring(e.message().c_str())); return StartupTaskState::Disabled; }
}

bool startupEnabled() {
    if (packaged()) { auto s = taskState(); return s == StartupTaskState::Enabled || s == StartupTaskState::EnabledByPolicy; }
    HKEY k; if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_READ, &k) != ERROR_SUCCESS) return false;
    DWORD type = 0, size = 0; bool on = RegQueryValueExW(k, L"minSolo", nullptr, &type, nullptr, &size) == ERROR_SUCCESS; RegCloseKey(k); return on;
}

bool startupBlocked() {
    if (!packaged()) return false;
    auto s = taskState();
    return s == StartupTaskState::DisabledByUser || s == StartupTaskState::DisabledByPolicy || s == StartupTaskState::EnabledByPolicy;
}

void setStartup(bool on) {
    if (packaged()) {
        try {
            auto task = StartupTask::GetAsync(kTaskId).get();
            if (on && task.State() == StartupTaskState::Disabled) task.RequestEnableAsync().get();
            else if (!on && task.State() == StartupTaskState::Enabled) task.Disable();
            logLine(L"startup task " + std::to_wstring((int)task.State()));
        } catch (const winrt::hresult_error& e) { logLine(L"StartupTask: " + std::wstring(e.message().c_str())); }
        return;
    }
    HKEY k; if (RegCreateKeyExW(HKEY_CURRENT_USER, kRunKey, 0, nullptr, 0, KEY_WRITE, nullptr, &k, nullptr) != ERROR_SUCCESS) return;
    if (on) { wchar_t path[MAX_PATH]; GetModuleFileNameW(nullptr, path, MAX_PATH); std::wstring v = L"\"" + std::wstring(path) + L"\""; RegSetValueExW(k, L"minSolo", 0, REG_SZ, (const BYTE*)v.c_str(), (DWORD)((v.size() + 1) * sizeof(wchar_t))); }
    else RegDeleteValueW(k, L"minSolo");
    RegCloseKey(k);
}

void openStartupSettings() { ShellExecuteW(nullptr, L"open", L"ms-settings:startupapps", nullptr, nullptr, SW_SHOWNORMAL); }
