#!/bin/bash
# One-command release verification: tests, universal build, signatures,
# self-test, every example graph and a MIDI export smoke test.
set -euo pipefail
root=$(cd "$(dirname "$0")/.." && pwd)
out=${1:-"$root/build/verify-release"}
mkdir -p "$out"
echo "== Soundnet release verification in $out"
echo "== version consistency"
"$root/scripts/check-version.sh"
echo "== engine and editor tests"
"$root/scripts/test-all.sh" "$out/tests"
echo "== app + AU build"
"$root/build-all.sh" "$out/build"
app="$out/build/Soundnet.app"
component="$out/build/Soundnet.component"
test -x "$app/Contents/MacOS/Soundnet"
echo "== codesign"
codesign --verify --deep --strict "$app"
codesign --verify --deep --strict "$component"
version=$(/usr/libexec/PlistBuddy -c 'Print :CFBundleShortVersionString' "$app/Contents/Info.plist")
build=$(/usr/libexec/PlistBuddy -c 'Print :CFBundleVersion' "$app/Contents/Info.plist")
echo "== version $version ($build)"
au_version=$(/usr/libexec/PlistBuddy -c 'Print :AudioComponents:0:version' "$component/Contents/Info.plist")
expected_au_version=$(python3 -c 'import sys; v=sys.argv[1].split("."); print((int(v[0])<<16)|(int(v[1])<<8)|int(v[2]))' "$version")
if [ "$au_version" != "$expected_au_version" ]; then
    echo "ERROR: AU component version $au_version != $expected_au_version" >&2
    exit 1
fi
echo "== AU component version $au_version"
echo "== self-test"
"$app/Contents/MacOS/Soundnet" --self-test
echo "== example graphs"
count=0
for example in "$root"/examples/*.json; do
    if ! grep -Eq '"version"[[:space:]]*:[[:space:]]*3' "$example"; then
        echo "   skip legacy format: $(basename "$example")"
        continue
    fi
    name=$(basename "$example" .json)
    "$app/Contents/MacOS/Soundnet" --validate "$example" >/dev/null
    "$app/Contents/MacOS/Soundnet" --export "$example" "$out/$name.wav" 1 >/dev/null
    count=$((count+1))
done
"$app/Contents/MacOS/Soundnet" --export-midi "$root/examples/clip-to-midi.json" "$out/clip-to-midi.mid" 2 >/dev/null
test -s "$out/clip-to-midi.mid"
echo "== $count example graphs and MIDI export passed"
if command -v auval >/dev/null 2>&1; then
    echo "== installed Audio Unit validation (may target an older install)"
    auval -v aumu Tern Trny 2>&1 | tail -1 || true
fi
echo "PASS: Soundnet $version ($build) release verification"
