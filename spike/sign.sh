#!/bin/zsh
# Build the spike and sign it with the Developer ID Application identity
# (hardened runtime, no timestamp: local use only). A stable identifier keeps
# the TCC grant across rebuilds.
set -euo pipefail
cd "$(dirname "$0")"
swift build -c release 2>&1 | tail -3
bin=.build/release/minsolo-spike
identity="$(security find-identity -v -p codesigning | grep -o '"Developer ID Application: [^"]*"' | head -1 | tr -d '"')"
if [[ -z "$identity" ]]; then echo "no Developer ID Application identity; signing ad-hoc" >&2; identity="-"; fi
codesign --force --options runtime --timestamp=none \
  --identifier com.bialkowski.minsolo.spike \
  --sign "$identity" "$bin"
codesign --verify --strict --verbose=2 "$bin"
codesign -d --entitlements - --verbose=1 "$bin" 2>&1 | grep -E 'Identifier|Authority|flags' | head -4
echo "built and signed: $PWD/$bin"
