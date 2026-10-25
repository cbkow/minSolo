#pragma once
// Launch at login and package identity.
//   Unpackaged (Inno, dev): the HKCU Run value "minSolo".
//   Packaged (Store MSIX): the manifest's startup task "minSoloStartup" (on after the first launch), through the
//   WinRT StartupTask API. A task the user switched off in Settings or Task Manager can't be turned back on by the
//   app (startupBlocked), so the menu sends them to Settings > Apps > Startup instead.

bool packaged();                // running with MSIX package identity
bool startupEnabled();
bool startupBlocked();          // packaged only: disabled (or enabled) by the user or policy outside the app
void setStartup(bool on);
void openStartupSettings();     // ms-settings:startupapps
