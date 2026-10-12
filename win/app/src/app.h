#pragma once
#include <windows.h>
#include "engine.h"
#include "sessions.h"
// Shared between main.cpp (tray, commands) and flyout.cpp (the popup UI).
extern Engine g_engine;
extern SessionWatcher g_watcher;
extern HWND g_hwnd;                 // the message-only tray window
void refresh();                     // snapshot + apply
void showSettingsMenu(HWND owner, POINT pt);   // the gear menu; dispatches its own commands
void quitApp();
