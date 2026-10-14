# minSolo for Windows

A tray app that mutes or solos individual apps' audio sessions. Muting happens inside the Windows
audio engine's own mix (`ISimpleAudioVolume::SetMute` on each session), so nothing is captured or
re-played and no latency is added. Plain Win32 + COM, no framework, one ~130 KB exe.

## Build

In a Visual Studio developer shell (VS 2022/2026, Ninja):

```powershell
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build                          # build\minSolo.exe (static CRT, no redistributable)
installer\build-setup.cmd                    # ..\..\dist\minSolo-<version>-Setup-x64.exe (Inno Setup 6)
cmake --build build --target minsolo_msix    # ..\..\dist\minSolo-<version>-win64.msix (unsigned = Store upload)
```

The installer is the release channel for now and is unsigned. For a signed
sideload MSIX set `MINSOLO_SIGNING_PFX` (absolute path), optionally
`MINSOLO_SIGNING_PASSWORD` and `MINSOLO_TIMESTAMP_URL`, before the
`minsolo_msix` target; the PFX subject must equal the manifest Publisher, and
signing must run on the logged-on desktop, not over SSH. Icons and Store
tiles are rendered from `../../assets/icons/minsoloWIN.svg` by
`../tools/make-ico.swift` and `../tools/make-tiles.swift`.

## Use

Click the tray icon: a flyout lists each app that is playing audio with an **M** (mute) and **S**
(solo) chip. Solo mutes every other app, including ones that start later, and leaves the soloed
app alone. **Clear all** releases everything. The gear opens the settings: show all audio apps,
remember mutes (mutes survive a relaunch; solos never do), solo also mutes system sounds, launch at
login (unpackaged builds; the packaged build uses the MSIX startup task in Settings > Apps >
Startup). Esc or a click elsewhere closes the flyout.

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
- `main.cpp`: message-only window, `Shell_NotifyIcon`, settings menu, 1 s refresh timer,
  single-instance mutex with `WM_COPYDATA` command forwarding, `TaskbarCreated` re-add.

Data: `%LOCALAPPDATA%\minSolo\settings.ini` and `minSolo.log` (works packaged and unpackaged;
packaged apps get a virtualized registry, so nothing is kept in HKCU except the unpackaged Run key).
