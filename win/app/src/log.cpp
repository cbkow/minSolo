#include "log.h"
#include <windows.h>
#include <shlobj.h>
#include <cstdio>
#include <mutex>

static std::wstring g_dir;
static std::mutex g_mu;

std::wstring dataDir() {
    if (!g_dir.empty()) return g_dir;
    PWSTR p = nullptr;
    std::wstring base = L".";
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &p)) && p) { base = p; CoTaskMemFree(p); }
    g_dir = base + L"\\minSolo";
    CreateDirectoryW(g_dir.c_str(), nullptr);
    return g_dir;
}

void logInit() { dataDir(); }

void logLine(const std::wstring& s) {
    std::lock_guard<std::mutex> lk(g_mu);
    FILE* f = _wfopen((dataDir() + L"\\minSolo.log").c_str(), L"a, ccs=UTF-8");
    if (!f) return;
    SYSTEMTIME t; GetLocalTime(&t);
    fwprintf(f, L"%04d-%02d-%02d %02d:%02d:%02d.%03d %s\n", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond, t.wMilliseconds, s.c_str());
    fclose(f);
}
