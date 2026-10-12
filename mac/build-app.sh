#!/bin/zsh
# Build minSolo, wrap it into minSolo.app, sign with Developer ID (hardened runtime).
#   ./build-app.sh            → build/minSolo.app
#   ./build-app.sh --run      → also (re)launch it
#   VERSION=0.1.0 ./build-app.sh
#   UNIVERSAL=1 ./build-app.sh   → arm64 + x86_64 (what build-pkg.sh uses)
set -euo pipefail
cd "$(dirname "$0")"
VERSION="${VERSION:-0.1.0}"
APP=build/minSolo.app

if [[ "${UNIVERSAL:-0}" == 1 ]]; then
  swift build -c release --arch arm64 --arch x86_64 2>&1 | grep -E 'error|Build complete' | tail -3
  BIN="$(swift build -c release --arch arm64 --arch x86_64 --show-bin-path)/minSolo"
else
  swift build -c release 2>&1 | grep -E 'error|Build complete' | tail -3
  BIN=.build/release/minSolo
fi

rm -rf "$APP"
mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Resources"
cp "$BIN" "$APP/Contents/MacOS/minSolo"
sed "s/@VERSION@/$VERSION/g" Info.plist > "$APP/Contents/Info.plist"
cp Resources/minsoloMAC.svg "$APP/Contents/Resources/"
if [[ ! -f build/minSolo.icns || ../assets/icons/minsoloMAC.svg -nt build/minSolo.icns ]]; then
  swift tools/make-icns.swift ../assets/icons/minsoloMAC.svg build/minSolo.icns
fi
cp build/minSolo.icns "$APP/Contents/Resources/minSolo.icns"
echo -n 'APPL????' > "$APP/Contents/PkgInfo"

identity="$(security find-identity -v -p codesigning | grep -o '"Developer ID Application: [^"]*"' | head -1 | tr -d '"')"
if [[ -z "$identity" ]]; then echo "no Developer ID Application identity; signing ad-hoc (TCC will re-prompt every build)" >&2; identity="-"; fi
codesign --force --options runtime --timestamp=none \
  --entitlements entitlements.plist --sign "$identity" "$APP"
codesign --verify --deep --strict --verbose=1 "$APP"
echo "built: $PWD/$APP ($VERSION)"

if [[ "${1:-}" == "--run" ]]; then
  pkill -x minSolo 2>/dev/null || true
  sleep 0.5
  open "$APP"
  echo "launched"
fi
