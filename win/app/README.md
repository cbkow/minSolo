# minSolo for Windows

A tray app that mutes or solos individual apps' audio sessions. Muting happens inside the Windows
audio engine's own mix (`ISimpleAudioVolume::SetMute` on each session), so nothing is captured or
re-played and no latency is added. Plain Win32 + COM (plus C++/WinRT for the Store startup task), no framework,
one ~330 KB exe.

## Build

In a Visual Studio developer shell (VS 2022/2026, Ninja):

```powershell
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build                          # build\minSolo.exe (static CRT, no redistributable)
cmake --build build --target minsolo_msix    # ..\..\dist\minSolo-<version>-win64.msix (unsigned = Store upload)
installer\build-setup.cmd                    # ..\..\dist\minSolo-<version>-Setup-x64.exe (Inno, unpublished fallback)
```

Since 0.1.2 the release channel is the **Microsoft Store**: [`msix/README.md`](msix/README.md) is the runbook
(identity, the signed sideload twin, what to verify on the box, submission text, per-release routine). The Inno
installer (`installer/`) is kept but no longer published. Icons and Store tiles are rendered from
`../../assets/icons/minsoloWIN.svg` by `../tools/make-ico.swift` and `../tools/make-tiles.swift`.

## Use

Click the tray icon: a flyout lists each app that is playing audio with an **M** (mute) and **S**
(solo) chip. Solo mutes every other app, including ones that start later, and leaves the soloed
app alone. **Clear all** releases everything. The gear opens the settings: show all audio apps,
remember mutes (mutes survive a relaunch; solos never do), solo also mutes system sounds, launch at
login (the Store build's startup task, on by default; the HKCU Run value when unpackaged). Esc or a click elsewhere closes the flyout.

Remote-testing note: if you listen to the machine through a remote-desktop client (Jump Desktop,
Parsec, RDP), its audio process is an "other app" too, so soloing anything mutes the stream you are
listening through.

Command line, forwarded to the running instance:

```
minSolo.exe --mute chrome.exe | --unmute chrome.exe | --solo chrome.exe | --unsolo chrome.exe | --clear | --quit | --show
```

## How it works

- `sessions.cpp`: enumerates every active render endpoint and its sessions (`IAudioSessionManager2`),
  groups by exe basename, and registers `IAudioSessionNotification` per endpoint (new sessions are
  muted at once when a mute or solo applies) plus `IMMNotificationClient` (device changes trigger a
  rescan). COM runs in the MTA, as the notification API requires. The notification callbacks only
  post to the UI thread, which does every audio-API call: calling back into the API, or releasing
  the last reference to one of its objects, from inside a notification deadlocks the process (the
  0.1.0 overnight tray freeze).
- `engine.cpp`: the muted/soloed sets and the policy. minSolo only ever unmutes sessions it muted;
  what the user mutes in the Volume Mixer is left alone. Windows persists per-app mute across process
  restarts, so the exes minSolo has touched are written to `settings.ini` and a later launch unmutes
  anything a crashed run left muted.
- `flyout.cpp`: the popup. Plain GDI, double-buffered, per-monitor DPI, follows the taskbar's
  light/dark theme, placed above (or below) the tray icon, closes on deactivate or Esc.
- `main.cpp`: a hidden top-level window (not message-only: sign-out and Store updates close the app with
  `WM_ENDSESSION`, which message-only windows never get; minSolo unmutes what it muted there), `Shell_NotifyIcon`,
  settings menu, 1 s refresh timer, single-instance mutex with `WM_COPYDATA` command forwarding, `TaskbarCreated`
  re-add, and `RegisterApplicationRestart` when packaged so a Store update relaunches it.
- `startup.cpp`: launch at login, the MSIX startup task through WinRT `StartupTask` or the HKCU Run value.

Data: `%LOCALAPPDATA%\minSolo\settings.ini` and `minSolo.log` (works packaged and unpackaged; a Store install on a
machine that never had the Inno build keeps them in the package's LocalCache, removed on uninstall; nothing is kept
in HKCU except the unpackaged Run key).
