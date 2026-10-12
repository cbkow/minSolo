# minSolo for Windows

A tray app that mutes or solos individual apps' audio sessions. Muting happens inside the Windows
audio engine's own mix (`ISimpleAudioVolume::SetMute` on each session), so nothing is captured or
re-played and no latency is added. Plain Win32 + COM, no framework, one ~130 KB exe.

## Build

In a Visual Studio developer shell (VS 2022/2026, Ninja):

```powershell
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
build\minSolo.exe
```

## Use

Left- or right-click the tray icon. Each app that is playing audio is a submenu with **Mute** and
**Solo**. Solo mutes every other app, including ones that start later, and leaves the soloed app
alone. **Clear all** releases everything. Settings: show all audio apps, remember mutes (mutes
survive a relaunch; solos never do), solo also mutes system sounds, launch at login (unpackaged
builds; the packaged build uses the MSIX startup task in Settings > Apps > Startup).

Command line, forwarded to the running instance:

```
minSolo.exe --mute chrome.exe | --unmute chrome.exe | --solo chrome.exe | --unsolo chrome.exe | --clear | --quit
```

## How it works

- `sessions.cpp`: enumerates every active render endpoint and its sessions (`IAudioSessionManager2`),
  groups by exe basename, and registers `IAudioSessionNotification` per endpoint (new sessions are
  muted at once when a mute or solo applies) plus `IMMNotificationClient` (device changes trigger a
  rescan). COM runs in the MTA, as the notification API requires.
- `engine.cpp`: the muted/soloed sets and the policy. minSolo only ever unmutes sessions it muted;
  what the user mutes in the Volume Mixer is left alone. Windows persists per-app mute across process
  restarts, so the exes minSolo has touched are written to `settings.ini` and a later launch unmutes
  anything a crashed run left muted.
- `main.cpp`: message-only window, `Shell_NotifyIcon`, popup menu, 1 s refresh timer, single-instance
  mutex with `WM_COPYDATA` command forwarding, `TaskbarCreated` re-add.

Data: `%LOCALAPPDATA%\minSolo\settings.ini` and `minSolo.log` (works packaged and unpackaged;
packaged apps get a virtualized registry, so nothing is kept in HKCU except the unpackaged Run key).
