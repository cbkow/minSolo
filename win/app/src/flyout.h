#pragma once
#include <windows.h>
void flyoutInit(HINSTANCE inst);
void flyoutToggle();     // open at the tray icon, or close if open
void flyoutShow();       // open (or re-place) at the tray icon
void flyoutClose();
