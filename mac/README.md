# minSolo for macOS

A Swift Package: SwiftUI `MenuBarExtra` on top of Core Audio process taps.

## How it works

- `AudioProcesses.swift` lists Core Audio process objects and groups helper
  processes under their owning app (responsible-PID lookup, then bundle-ID
  prefix), refreshing on process-list changes and a 1 s poll.
- `TapEngine.swift` holds the muted and soloed sets. A mute is a
  `CATapMuted` tap on that app's processes; a solo is one global tap
  excluding the soloed apps. A muted tap only silences while a reader is
  attached, so every tap gets a private aggregate device with an idle
  IOProc. Taps are updated in place when processes come and go, readers are
  rebuilt when the default output device changes, leftover taps from a
  crashed run are swept on launch (taps outlive their creator), and
  everything is released on quit or SIGTERM.
- `App.swift` is the popover: a row per app with M and S, Clear all, the
  gear menu (show all audio apps, remember mutes, launch at login), Quit.

## Build

```bash
./build-app.sh --run          # swift build, wrap build/minSolo.app, codesign, launch
UNIVERSAL=1 ./build-app.sh    # arm64 + x86_64
```

Dev builds are signed with the Developer ID Application identity so the
System Audio Recording grant survives rebuilds; ad-hoc signing changes the
code hash every build and macOS re-prompts each launch. The icon is
rendered from `../assets/icons/minsoloMAC.svg` by `tools/make-icns.swift`.

## Release

```bash
VERSION=0.1.2 ./packaging/build-pkg.sh --notarize
# → ../dist/minSolo-0.1.2-universal.pkg
```

The same flow as the sister repos: a universal build, re-signed with
hardened runtime and timestamp, notarized and stapled as an app, then
`pkgbuild`/`productbuild` with the Developer ID Installer identity,
notarized and stapled again. Two submissions, 2–15 minutes each. Needs the
`QCView` notarytool keychain profile (`MINSOLO_NOTARY_PROFILE` overrides);
nothing secret is in the repository. `packaging/notarize.sh <file>` handles
one file on its own. Afterwards:

```bash
spctl --assess --type install -vv ../dist/minSolo-<version>-universal.pkg
xcrun stapler validate /Applications/minSolo.app
```

The floor is macOS 14.2, where `AudioHardwareCreateProcessTap` appears.
