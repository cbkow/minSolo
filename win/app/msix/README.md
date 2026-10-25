# minSolo on Windows — the Microsoft Store package

Since 0.1.2, Windows ships through the **Microsoft Store**, like minRender, minNotes and ufb. The Store signs the
MSIX on ingestion (no Authenticode certificate, no SmartScreen prompt) and delivers updates; minSolo never had a
self-updater, so nothing was retired for it. `../installer/minSolo.iss` (Inno Setup) is kept as an unpublished
fallback only. macOS is unchanged: the signed, notarized `.pkg` (`mac/packaging/`).

Ported from QCView-Player (`installer/msix/`); the certification lessons come from minRender (`installer/STORE.md`).

## Layout

```
win/app/msix/
├── AppxManifest.xml.in   the manifest (configure_file @ONLY from ../CMakeLists.txt)
├── build_msix.ps1        guard capabilities → stage → MakePri → MakeAppx → optional signtool
└── images/               tiles, rendered from assets/icons/minsoloWIN.svg by ../../tools/make-tiles.swift
```

Identity (Partner Center → Product management → Product identity), cache variables in `../CMakeLists.txt`:
`cbkow.minSolo` / `CN=C64A1D41-EFAA-4EED-AE81-0C074867D4E9` / `cbkow`, the same publisher as the sister apps
(ingestion rejects a mismatch). Package family name `cbkow.minSolo_k3nw8mfd599r4`. The MSIX version is
`PROJECT_VERSION` + `.0`.

## Build

From a VS x64 developer shell, after the release build (`../README.md`):

```powershell
cmake --build build --target minsolo_msix
```

Output: `dist\minSolo-<version>-win64.msix` at the repo root, **unsigned**: the file Partner Center takes. The exe
links the C runtime statically, so there is nothing to bundle beside it. `build_msix.ps1` strips the template's
comments from the packaged manifest and refuses to pack unless the capabilities are exactly `runFullTrust` with
virtualization left on (minRender 0.5.6.0 was denied for `unvirtualizedResources`).

## Signed sideload twin (testing before certification)

Set `MINSOLO_SIGNING_PFX` (ABSOLUTE path to the family's dev PFX, the QCView-Player one on the box),
`MINSOLO_SIGNING_PASSWORD` and `MINSOLO_TIMESTAMP_URL` (`http://timestamp.digicert.com`), then build the same
target: it writes `minSolo-<version>-win64-sideload.msix` and checks it with `signtool verify /pa`. Move the unsigned
Store file aside first. signtool's PFX import and `Add-AppxPackage` / `Remove-AppxPackage` fail over SSH: run them
on the logged-on desktop (`C:\qcb-lab\run.ps1`). Uninstall a sideload before the Store version arrives; a sideloaded
package with a higher version blocks the Store one.

## What the package does and doesn't need

- **Capabilities: `runFullTrust` only.** minSolo is a Win32 tray app; muting other apps goes through the audio
  engine's per-app sessions (`IAudioSessionManager2` / `ISimpleAudioVolume`), which a full-trust packaged process
  reaches exactly like an unpackaged one. No network, no restricted capability beyond full trust.
- **Data:** `%LOCALAPPDATA%\minSolo\settings.ini` and `minSolo.log`, read only by minSolo, so default virtualization
  is fine. On a machine that never ran the Inno build they live in
  `%LOCALAPPDATA%\Packages\cbkow.minSolo_k3nw8mfd599r4\LocalCache\Local\minSolo`, and uninstall removes them. A
  machine that had the Inno build keeps using the real folder.
- **Startup task** (`minSoloStartup`, on after the first launch): starts minSolo in the tray at sign-in. The gear
  menu's **Launch at login** toggles it through the WinRT `StartupTask` API (`src/startup.cpp`); once the user has
  switched it off in Settings or Task Manager only they can switch it on again, so the item then opens
  Settings → Apps → Startup.
- **Execution alias** `minSolo.exe`: `minSolo.exe --mute chrome.exe` and the other switches reach the running
  instance from any terminal. It belongs to the one visible Application, so it needs no hidden-app waiver (the
  `HeadlessAppBypass` refusal minRender hit was for a second, hidden Application entry).
- **Store updates and sign-out:** servicing closes the app with `WM_QUERYENDSESSION` / `WM_ENDSESSION`
  (`ENDSESSION_CLOSEAPP`), which only top-level windows receive, so the tray window is a hidden top-level window
  rather than message-only. On that message minSolo unmutes everything it muted; a mute left behind would persist,
  because Windows remembers per-app mute. Packaged runs call `RegisterApplicationRestart`, so Windows relaunches
  minSolo from the new version, which re-applies the remembered mutes (solos are not remembered).
- **Minimum OS: Windows 10 1809 (10.0.17763)**, like the Inno installer.

## Verify on the box (sideload, desktop session)

After any manifest or packaging change. Verified for 0.1.2 on 2026-10-25:

1. `Add-AppxPackage` the sideload; it launches from Start, the log says `started (packaged)` and
   `registered for restart after updates`.
2. Startup task enabled after the first launch (`State` = 2 under
   `HKCU\Software\Classes\Local Settings\Software\Microsoft\Windows\CurrentVersion\AppModel\SystemAppData\<pfn>\minSoloStartup`);
   the gear menu's **Launch at login** shows it checked and toggles it off (0) and on (2).
3. With `playerb.exe` playing on the desktop, `%LOCALAPPDATA%\Microsoft\WindowsApps\minSolo.exe --mute playerb.exe`
   mutes it in the running instance.
4. Update rehearsal: build a copy with the manifest version + 1 in the fourth field, sign it, wait 60 s after launch,
   then `Add-AppxPackage <newer> -ForceApplicationShutdown`. The log shows `session ending (close-app request…)`,
   `unmuted playerb.exe`, `stopped`, then a new process from the newer package folder that logs
   `restored mutes: playerb.exe` and mutes it again (0.5 s on the box).
5. `Remove-AppxPackage`: the mute is released first; package, alias, startup task and LocalCache are gone.

## Submission

1. Build the unsigned MSIX from the release commit; Defender-scan it on the box
   (`MpCmdRun.exe -Scan -ScanType 3 -File <msix> -DisableRemediation`).
2. Partner Center → minSolo → new submission: upload the `.msix`, choose **Publish this submission manually**.
   Only one submission can be in flight per app.
3. **Restricted capabilities → runFullTrust:** "minSolo is a Win32 tray app. It mutes and unmutes other apps' audio
   through the Windows audio engine's per-app audio sessions (IAudioSessionManager2 / ISimpleAudioVolume, the same
   sessions the Volume Mixer controls). It captures and records nothing, makes no network connections, and runs no
   code other than its own exe."
4. **License terms:** MIT (`LICENSE`). **Privacy policy:**
   <https://github.com/cbkow/minSolo/blob/main/PRIVACY_POLICY.md>. **Website / support:**
   <https://github.com/cbkow/minSolo>, <https://github.com/cbkow/minSolo/issues>.
5. Certification typically takes 1–3 business days. A denied package gets a version bump before resubmitting.

### Listing text (draft)

> Mute or solo any app's audio from the system tray. Click the minSolo icon to see every app that is playing sound,
> each with an M (mute) and S (solo) button. Solo silences every other app, including ones that start later, so you
> can listen to one thing without closing the rest. minSolo works inside the Windows audio engine's own mix, the
> same per-app volumes the Volume Mixer shows: nothing is captured or re-routed, no latency is added, and
> audio/video sync is untouched. Optional: remember mutes across restarts, include system sounds in a solo, and
> control it from the command line (minSolo.exe --mute chrome.exe).

### Notes for certification (draft)

```
minSolo is a small Win32 tray app; it has no main window. After launch, click its icon in the notification area
(it may sit in the overflow chevron) to open the flyout listing the apps currently playing audio.

To test: play audio in two apps (for example a browser and the Media Player), open the flyout, press M on one to
mute it, or S to solo it (every other app is muted). "Clear all" releases everything. The gear menu holds the
options and "Launch at login".

Expected behavior: a startup task starts minSolo in the tray at sign-in; users turn it off in the gear menu,
Settings > Apps > Startup or Task Manager. Quitting, signing out or updating unmutes every app minSolo muted. The
minSolo.exe execution alias forwards switches such as --mute chrome.exe to the running instance.

No network access, no account, no data collected. Uninstall removes the app, its settings and the startup task.
```

## Per-release routine

1. Bump `project(minSolo VERSION …)` in `../CMakeLists.txt` (the MSIX version follows) and, for parity, `VERSION=`
   in `mac/build-app.sh`.
2. macOS: `mac/packaging/build-pkg.sh --notarize` as before.
3. Windows: release build + `minsolo_msix` on the box; sideload-smoke-test (above) when the release touches
   packaging, startup or shutdown; Defender-scan; submit; publish when certified.
