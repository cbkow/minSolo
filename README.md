# minSolo

Mute or solo individual apps' audio from the menu bar (macOS) or system tray (Windows), with zero added latency for everything you can still hear.

- **macOS 14.2+:** Core Audio process taps. A muted app is dropped from the mix; every other app keeps its direct path to the hardware. Nothing is captured and played back.
- **Windows 10 1809+:** WASAPI per-app audio sessions, muted inside the Windows audio engine. System tray app.

## Install (macOS)

Download the latest `minSolo-<version>-universal.pkg` from [Releases](https://github.com/cbkow/minSolo/releases) and open it. The installer is signed and notarized; it puts minSolo.app in /Applications. On first mute, allow **System Audio Recording** when macOS asks.

## Layout

| Path | What |
|---|---|
| `mac/` | The macOS app: Swift Package, SwiftUI `MenuBarExtra`, `build-app.sh` builds + signs `build/minSolo.app` |
| `spike/` | The signed CLI used to answer the Core Audio questions (`spike/README.md` has the findings) |
| `win/app/` | The Windows app: C++/Win32, CMake, `msix/` holds the Store manifest, tiles and packaging script |
| `win/spike/` | The WASAPI session CLI used to answer the Windows questions (`win/spike/README.md`) |
| `assets/icons/` | The mark, as SVG for macOS (transparent) and Windows (dark tile) |

## Build (macOS)

```bash
cd mac && ./build-app.sh --run     # swift build, wrap, codesign (Developer ID), launch
```

Dev builds are signed with the Developer ID Application identity so the System Audio Recording grant survives rebuilds. Without an identity the script falls back to ad-hoc signing and macOS re-prompts every build.

## Release (macOS)

```bash
cd mac && VERSION=0.1.0 ./packaging/build-pkg.sh --notarize
# → dist/minSolo-0.1.0-universal.pkg, signed with Developer ID Installer,
#   app and pkg both notarized and stapled (two submissions, 2–15 min each)
```

Same flow as the sister repos: everything is a script under `mac/packaging/`, nothing secret is in the repository, the notarytool credentials live in the `QCView` keychain profile. `packaging/notarize.sh <file>` notarizes and staples one file on its own. Check a build with:

```bash
spctl --assess --type install -vv dist/minSolo-<version>-universal.pkg
xcrun stapler validate /Applications/minSolo.app
```

## Build (Windows)

In a Visual Studio developer shell:

```powershell
cd win\app
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build                          # build\minSolo.exe
cmake --build build --target minsolo_msix    # dist\minSolo-<version>-win64.msix (unsigned = Store upload)
```

Set `MINSOLO_SIGNING_PFX` (absolute path), `MINSOLO_SIGNING_PASSWORD` and `MINSOLO_TIMESTAMP_URL` before the `minsolo_msix` target to get a signed `-sideload.msix` instead; the PFX subject must equal the manifest Publisher. Signing has to run on the logged-on desktop, not over SSH. See `win/app/README.md` for how the app works and its command-line switches.

## Settings

The gear menu in the popover has **Show all audio apps** (every app with an audio connection, not only the ones playing), **Remember mutes** (mutes survive a relaunch; solos never do) and **Launch at login**.

## Privacy

Nothing is recorded or sent anywhere. See [PRIVACY_POLICY.md](PRIVACY_POLICY.md).

## License

[MIT](LICENSE)
