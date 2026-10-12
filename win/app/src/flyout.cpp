// The popup: one row per app (icon, name, M and S chips), a footer with Clear all, settings, quit.
// Plain GDI, double-buffered, follows the taskbar's light/dark theme, closes on click-away or Esc.
#include "flyout.h"
#include "app.h"
#include "log.h"
#include <dwmapi.h>
#include <windowsx.h>
#include <shellapi.h>
#include <map>
#include <string>
#include <vector>
#pragma comment(lib, "dwmapi.lib")

static const wchar_t* kFlyoutClass = L"minSoloFlyout";
static HWND g_fly;
static HINSTANCE g_inst;
static std::vector<AppEntry> g_rows;
static int g_dpi = 96;
static HFONT g_font, g_fontSmall, g_fontChip;
static std::map<std::wstring, HICON> g_icons;

struct Hit { RECT r; int kind; int row; };   // kind: 1 mute chip, 2 solo chip, 3 clear, 4 settings, 5 quit
static std::vector<Hit> g_hits;
static int g_hover = -1;

static int S(int px) { return MulDiv(px, g_dpi, 96); }

struct Theme { COLORREF bg, text, sub, chip, chipText, divider, mute, solo, onText, hover; };
static Theme theme() {
    DWORD light = 1, sz = sizeof(light);
    RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize", L"SystemUsesLightTheme", RRF_RT_REG_DWORD, nullptr, &light, &sz);
    Theme t;
    if (light == 0) { t.bg = RGB(32, 32, 32); t.text = RGB(240, 240, 240); t.sub = RGB(150, 150, 150); t.chip = RGB(58, 58, 58); t.chipText = RGB(220, 220, 220); t.divider = RGB(60, 60, 60); t.hover = RGB(72, 72, 72); }
    else { t.bg = RGB(243, 243, 243); t.text = RGB(20, 20, 20); t.sub = RGB(110, 110, 110); t.chip = RGB(222, 222, 222); t.chipText = RGB(40, 40, 40); t.divider = RGB(210, 210, 210); t.hover = RGB(205, 205, 205); }
    t.mute = RGB(0xdd, 0x4e, 0x47);   // the brand red
    t.solo = RGB(0xe6, 0xb4, 0x22);
    t.onText = RGB(20, 20, 20);
    return t;
}

static void makeFonts() {
    if (g_font) { DeleteObject(g_font); DeleteObject(g_fontSmall); DeleteObject(g_fontChip); }
    g_font = CreateFontW(-MulDiv(9, g_dpi, 72), 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
    g_fontSmall = CreateFontW(-MulDiv(8, g_dpi, 72), 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
    g_fontChip = CreateFontW(-MulDiv(9, g_dpi, 72), 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
}

static HICON iconFor(const AppEntry& a) {
    auto it = g_icons.find(a.exePath);
    if (it != g_icons.end()) return it->second;
    HICON h = nullptr;
    if (!a.exePath.empty()) {
        SHFILEINFOW sfi{};
        if (SHGetFileInfoW(a.exePath.c_str(), 0, &sfi, sizeof(sfi), SHGFI_ICON | SHGFI_LARGEICON)) h = sfi.hIcon;
    }
    g_icons[a.exePath] = h;
    return h;
}

// Layout at the current DPI.
static const int W = 300, PAD = 12, ROW = 40, CHIP_W = 28, CHIP_H = 22, CHIP_GAP = 6, FOOTER = 34, ICON = 20;

static int contentHeight() {
    int rows = (int)g_rows.size();
    return S(PAD) + S(ROW) * (rows ? rows : 1) + S(6) + 1 + S(6) + S(FOOTER) + S(PAD) / 2;
}

static void rebuildRows() {
    auto sessions = g_watcher.snapshot();
    g_engine.apply(sessions);
    g_rows = g_engine.apps(sessions);
}

static void fillRound(HDC dc, const RECT& r, COLORREF c, int radius) {
    HBRUSH b = CreateSolidBrush(c); HPEN p = CreatePen(PS_SOLID, 1, c);
    HGDIOBJ ob = SelectObject(dc, b), op = SelectObject(dc, p);
    RoundRect(dc, r.left, r.top, r.right, r.bottom, radius, radius);
    SelectObject(dc, ob); SelectObject(dc, op); DeleteObject(b); DeleteObject(p);
}

static void paint(HDC dc, const RECT& client) {
    Theme t = theme();
    g_hits.clear();
    HBRUSH bg = CreateSolidBrush(t.bg); FillRect(dc, &client, bg); DeleteObject(bg);
    SetBkMode(dc, TRANSPARENT);
    int x0 = S(PAD), y = S(PAD), w = client.right;
    int chipX2 = w - S(PAD) - S(CHIP_W), chipX1 = chipX2 - S(CHIP_GAP) - S(CHIP_W);

    if (g_rows.empty()) {
        SelectObject(dc, g_font); SetTextColor(dc, t.sub);
        RECT r{x0, y, w - S(PAD), y + S(ROW)};
        DrawTextW(dc, g_engine.showAll ? L"No apps are using audio" : L"No apps are playing audio", -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        y += S(ROW);
    }
    for (size_t i = 0; i < g_rows.size(); ++i) {
        const auto& a = g_rows[i];
        int cy = y + S(ROW) / 2;
        if (HICON ic = iconFor(a)) DrawIconEx(dc, x0, cy - S(ICON) / 2, ic, S(ICON), S(ICON), 0, nullptr, DI_NORMAL);
        int tx = x0 + S(ICON) + S(10);
        RECT nameR{tx, y, chipX1 - S(8), y + S(ROW)};
        std::wstring sub = a.sessions == 0 ? L"not running" : (!a.active ? L"silent" : L"");
        SelectObject(dc, g_font); SetTextColor(dc, t.text);
        if (sub.empty()) DrawTextW(dc, a.name.c_str(), -1, &nameR, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
        else {
            RECT r1{tx, y + S(5), nameR.right, cy + S(1)}, r2{tx, cy, nameR.right, y + S(ROW)};
            DrawTextW(dc, a.name.c_str(), -1, &r1, DT_LEFT | DT_BOTTOM | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
            SelectObject(dc, g_fontSmall); SetTextColor(dc, t.sub);
            DrawTextW(dc, sub.c_str(), -1, &r2, DT_LEFT | DT_TOP | DT_SINGLELINE);
        }
        bool m = g_engine.isMuted(a.exe), s = g_engine.isSoloed(a.exe);
        RECT cm{chipX1, cy - S(CHIP_H) / 2, chipX1 + S(CHIP_W), cy + S(CHIP_H) / 2};
        RECT cs{chipX2, cy - S(CHIP_H) / 2, chipX2 + S(CHIP_W), cy + S(CHIP_H) / 2};
        int hm = (int)g_hits.size(); g_hits.push_back({cm, 1, (int)i});
        int hs = (int)g_hits.size(); g_hits.push_back({cs, 2, (int)i});
        fillRound(dc, cm, m ? t.mute : (g_hover == hm ? t.hover : t.chip), S(6));
        fillRound(dc, cs, s ? t.solo : (g_hover == hs ? t.hover : t.chip), S(6));
        SelectObject(dc, g_fontChip);
        SetTextColor(dc, m ? t.onText : t.chipText); DrawTextW(dc, L"M", 1, &cm, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        SetTextColor(dc, s ? t.onText : t.chipText); DrawTextW(dc, L"S", 1, &cs, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        y += S(ROW);
    }
    y += S(6);
    HPEN pen = CreatePen(PS_SOLID, 1, t.divider); HGDIOBJ op = SelectObject(dc, pen);
    MoveToEx(dc, x0, y, nullptr); LineTo(dc, w - S(PAD), y); SelectObject(dc, op); DeleteObject(pen);
    y += 1 + S(6);

    // Footer: Clear all | gear | Quit
    SelectObject(dc, g_font);
    bool canClear = !(g_engine.muted.empty() && g_engine.soloed.empty());
    RECT rc{x0, y, x0 + S(70), y + S(FOOTER)};
    int hc = (int)g_hits.size(); g_hits.push_back({rc, 3, -1});
    SetTextColor(dc, canClear ? (g_hover == hc ? t.text : t.sub) : t.divider);
    DrawTextW(dc, L"Clear all", -1, &rc, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    RECT rq{w - S(PAD) - S(40), y, w - S(PAD), y + S(FOOTER)};
    int hq = (int)g_hits.size(); g_hits.push_back({rq, 5, -1});
    SetTextColor(dc, g_hover == hq ? t.text : t.sub);
    DrawTextW(dc, L"Quit", -1, &rq, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
    RECT rg{rq.left - S(36), y, rq.left - S(8), y + S(FOOTER)};
    int hg = (int)g_hits.size(); g_hits.push_back({rg, 4, -1});
    SetTextColor(dc, g_hover == hg ? t.text : t.sub);
    HFONT sym = CreateFontW(-MulDiv(11, g_dpi, 72), 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe MDL2 Assets");
    HGDIOBJ of = SelectObject(dc, sym);
    DrawTextW(dc, L"\xE713", 1, &rg, DT_CENTER | DT_VCENTER | DT_SINGLELINE);   // Settings glyph
    SelectObject(dc, of); DeleteObject(sym);
}

static int hitAt(POINT p) {
    for (size_t i = 0; i < g_hits.size(); ++i) if (PtInRect(&g_hits[i].r, p)) return (int)i;
    return -1;
}

static void place() {
    RECT icon{};
    NOTIFYICONIDENTIFIER nii{sizeof(nii)}; nii.hWnd = g_hwnd; nii.uID = 1;
    if (FAILED(Shell_NotifyIconGetRect(&nii, &icon))) GetCursorPos((POINT*)&icon), icon.right = icon.left, icon.bottom = icon.top;
    HMONITOR mon = MonitorFromRect(&icon, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi{sizeof(mi)}; GetMonitorInfoW(mon, &mi);
    UINT dpi = GetDpiForWindow(g_fly); if (dpi) g_dpi = (int)dpi;
    makeFonts();
    int w = S(W), h = contentHeight(), gap = S(8);
    int x = (icon.left + icon.right) / 2 - w / 2;
    int y = icon.top - h - gap;
    RECT wa = mi.rcWork;
    if (icon.top > (wa.top + wa.bottom) / 2) y = icon.top - h - gap; else y = icon.bottom + gap;   // taskbar bottom vs top
    if (x + w > wa.right - gap) x = wa.right - gap - w;
    if (x < wa.left + gap) x = wa.left + gap;
    if (y < wa.top + gap) y = wa.top + gap;
    SetWindowPos(g_fly, HWND_TOPMOST, x, y, w, h, SWP_NOACTIVATE);
    logLine(L"flyout at " + std::to_wstring(x) + L"," + std::to_wstring(y) + L" " + std::to_wstring(w) + L"x" + std::to_wstring(h)
            + L" (tray icon " + std::to_wstring(icon.left) + L"," + std::to_wstring(icon.top) + L"-" + std::to_wstring(icon.right) + L"," + std::to_wstring(icon.bottom)
            + L", work " + std::to_wstring(wa.left) + L"," + std::to_wstring(wa.top) + L"-" + std::to_wstring(wa.right) + L"," + std::to_wstring(wa.bottom) + L", dpi " + std::to_wstring(g_dpi) + L")");
}

static LRESULT CALLBACK flyProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_PAINT: {
        PAINTSTRUCT ps; HDC dc = BeginPaint(h, &ps);
        RECT c; GetClientRect(h, &c);
        HDC mem = CreateCompatibleDC(dc); HBITMAP bmp = CreateCompatibleBitmap(dc, c.right, c.bottom);
        HGDIOBJ ob = SelectObject(mem, bmp);
        paint(mem, c);
        BitBlt(dc, 0, 0, c.right, c.bottom, mem, 0, 0, SRCCOPY);
        SelectObject(mem, ob); DeleteObject(bmp); DeleteDC(mem);
        EndPaint(h, &ps); return 0;
    }
    case WM_ERASEBKGND: return 1;
    case WM_MOUSEMOVE: {
        TRACKMOUSEEVENT tme{sizeof(tme), TME_LEAVE, h, 0}; TrackMouseEvent(&tme);
        int hit = hitAt({GET_X_LPARAM(l), GET_Y_LPARAM(l)});
        if (hit != g_hover) { g_hover = hit; InvalidateRect(h, nullptr, FALSE); }
        return 0;
    }
    case WM_MOUSELEAVE: if (g_hover != -1) { g_hover = -1; InvalidateRect(h, nullptr, FALSE); } return 0;
    case WM_LBUTTONUP: {
        int hit = hitAt({GET_X_LPARAM(l), GET_Y_LPARAM(l)});
        if (hit < 0) return 0;
        const Hit& ht = g_hits[hit];
        switch (ht.kind) {
        case 1: { auto& e = g_rows[ht.row].exe; g_engine.setMuted(e, !g_engine.isMuted(e)); refresh(); break; }
        case 2: { auto& e = g_rows[ht.row].exe; g_engine.setSoloed(e, !g_engine.isSoloed(e)); refresh(); break; }
        case 3: g_engine.clearAll(); refresh(); break;
        case 4: { POINT pt{ht.r.left, ht.r.top}; ClientToScreen(h, &pt); showSettingsMenu(h, pt); break; }
        case 5: flyoutClose(); quitApp(); return 0;
        }
        rebuildRows(); place(); InvalidateRect(h, nullptr, FALSE);
        return 0;
    }
    case WM_KEYDOWN: if (w == VK_ESCAPE) flyoutClose(); return 0;
    case WM_ACTIVATE: if (LOWORD(w) == WA_INACTIVE) { logLine(L"flyout deactivated → close"); flyoutClose(); } return 0;
    case WM_TIMER: { auto before = g_rows; rebuildRows(); if (!(before == g_rows)) { place(); InvalidateRect(h, nullptr, FALSE); } return 0; }
    case WM_DPICHANGED: g_dpi = HIWORD(w); makeFonts(); place(); return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

void flyoutInit(HINSTANCE inst) {
    g_inst = inst;
    WNDCLASSW wc{}; wc.lpfnWndProc = flyProc; wc.hInstance = inst; wc.lpszClassName = kFlyoutClass;
    wc.style = CS_DROPSHADOW | CS_HREDRAW | CS_VREDRAW; wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    RegisterClassW(&wc);
    g_fly = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST, kFlyoutClass, L"minSolo", WS_POPUP, 0, 0, S(W), 100, nullptr, nullptr, inst, nullptr);
    DWORD corner = 2;  // DWMWCP_ROUND (Windows 11; ignored on 10)
    DwmSetWindowAttribute(g_fly, 33 /*DWMWA_WINDOW_CORNER_PREFERENCE*/, &corner, sizeof(corner));
    g_dpi = GetDpiForWindow(g_fly); if (!g_dpi) g_dpi = 96;
    makeFonts();
}

void flyoutToggle() {
    if (IsWindowVisible(g_fly)) { flyoutClose(); return; }
    flyoutShow();
}

void flyoutShow() {
    rebuildRows(); g_hover = -1;
    place();
    ShowWindow(g_fly, SW_SHOW);
    SetForegroundWindow(g_fly);
    SetTimer(g_fly, 1, 1000, nullptr);
}

void flyoutClose() {
    if (!IsWindowVisible(g_fly)) return;
    KillTimer(g_fly, 1);
    ShowWindow(g_fly, SW_HIDE);
}
