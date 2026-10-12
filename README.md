# minSolo

Mute or solo any app's audio from the menu bar (macOS) or the tray (Windows). This isn't a sound mixer that captures and routes audio; instead, it uses native Core Audio/WASAPI hooks to list apps and let you mute or solo them. This adds zero latency to audio sources.

- **macOS 14.2 or later** (Apple silicon and Intel): Core Audio process
  taps. The first mute asks for *System Audio Recording* permission, which
  is how macOS gates taps; the tapped audio is discarded at once and
  nothing is recorded.
- **Windows 10 1809 or later** (x64): the per-app audio sessions of the
  Windows audio engine, the same ones the Volume Mixer controls.

## Install (macOS)

Download `minSolo-<version>-universal.pkg` from
[Releases](../../releases/latest) and run it. The installer is signed and
notarized; it puts `minSolo.app` in /Applications and the mark appears in
the menu bar. Allow **System Audio Recording** when asked.

## Install (Windows)

Run `minSolo-<version>-Setup-x64.exe` from [Releases](../../releases/latest).
It installs one exe under `Program Files\minSolo`, with an optional "start
when you sign in" task, and the mark appears in the tray. The installer is
not code-signed yet, so SmartScreen asks once: **More info → Run anyway**.

## Good to know

- A muted app stays muted only while minSolo runs. Quitting releases
  everything; so does **Clear all**.
- Apps in exclusive mode (hog mode on macOS, WASAPI exclusive or ASIO on
  Windows) bypass the mixer and cannot be muted.
- If you listen to a machine through a remote-desktop client, that client's
  audio process is an app too: soloing anything mutes the stream you are
  listening through.
- Windows: `minSolo.exe --mute chrome.exe`, `--solo`, `--unmute`,
  `--unsolo`, `--clear` reach the running instance from any terminal.

## Building

- macOS: [`mac/README.md`](mac/README.md) (Swift Package, signed pkg).
- Windows: [`win/app/README.md`](win/app/README.md) (C++/Win32, CMake,
  Inno Setup and MSIX).
- `spike/` and `win/spike/` are the command-line probes that settled the
  platform questions; their READMEs hold the findings.

## Privacy and license

Nothing is collected or sent anywhere: [PRIVACY_POLICY.md](PRIVACY_POLICY.md).
MIT, see [LICENSE](LICENSE). minSolo uses only the platform SDKs; there is
no third-party code.
