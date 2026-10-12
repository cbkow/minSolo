#pragma once
#include <string>
// Appends to %LOCALAPPDATA%\minSolo\minSolo.log (works packaged and unpackaged).
void logInit();
void logLine(const std::wstring& s);
std::wstring dataDir();   // %LOCALAPPDATA%\minSolo, created
