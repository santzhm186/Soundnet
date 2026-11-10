#!/bin/bash
# One-command release verification: tests, universal build, signatures,
# self-test, every example graph and a MIDI export smoke test.
# Usage: verify-release.sh [output-directory] [--skip-tests | --verify-built]
# --skip-tests is for resuming build checks after this source's suite has passed.
# --verify-built validates existing artifacts after an isolated rebuild.
set -euo pipefail
unset SOUNDNET_BUILD_ARCH
root=$(cd "$(dirname "$0")/.." && pwd)
python3 "$root/scripts/generate-node-registry.py"
python3 "$root/scripts/generate-node-registry.py" --check
out=${1:-"$root/build/verify-release"}
mkdir -p "$out"
echo "== Soundnet release verification in $out"
echo "== version consistency"
"$root/scripts/check-version.sh"
case "${2:-}" in
    "")
        echo "== engine and editor tests"
        "$root/scripts/test-all.sh" "$out/tests" ;;
    --skip-tests|--verify-built)
        echo "== reusing separately completed tests; this invocation verifies build and runtime artifacts" ;;
    *) echo "Usage: $0 [output-directory] [--skip-tests | --verify-built]" >&2; exit 2 ;;
esac
if [[ "${2:-}" == --verify-built ]]; then
    echo "== verifying existing app + AU build"
else
    echo "== app + AU build"
    "$root/build-all.sh" "$out/build"
fi
app="$out/build/Soundnet.app"
component="$out/build/Soundnet.component"
test -x "$app/Contents/MacOS/Soundnet"
export SOUNDNET_NODE_PATH="$app/Contents/Resources/Nodes"
echo "== codesign"
codesign --verify --deep --strict "$app"
codesign --verify --deep --strict "$component"
echo "== universal host, SDK and node binaries"
python3 - "$out/build" <<'PY'
from pathlib import Path
import subprocess, sys
root = Path(sys.argv[1])
binaries = [root / 'Soundnet.app/Contents/MacOS/Soundnet',
            root / 'Soundnet.component/Contents/MacOS/Soundnet',
            root / 'Soundnet.component/Contents/Resources/TernaryView.bundle/Contents/MacOS/TernaryView',
            root / 'Soundnet-Node-SDK/lib/libSoundnetPlatform.dylib']
binaries += list(root.glob('Soundnet.*/Contents/Frameworks/*.dylib'))
binaries += list(root.glob('Soundnet.*/Contents/Resources/Nodes/*.soundnode/Node.dylib'))
for binary in binaries:
    architectures = subprocess.check_output(['lipo', '-archs', str(binary)], text=True).split()
    if set(architectures) != {'arm64', 'x86_64'}:
        raise SystemExit(f'{binary}: expected arm64 + x86_64, got {architectures}')
print(f'PASS: {len(binaries)} universal binaries')
PY
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
    if ! grep -Eq '"version"[[:space:]]*:[[:space:]]*[34]' "$example"; then
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
"$app/Contents/MacOS/Soundnet" --export "$root/examples/audio-clip-compressor.json" "$out/format-44k1-24.wav" 1 44100 24 >/dev/null
test -s "$out/format-44k1-24.wav"
"$app/Contents/MacOS/Soundnet" --export "$root/examples/audio-clip-compressor.json" "$out/format-48k-32f.wav" 1 48000 32 >/dev/null
test -s "$out/format-48k-32f.wav"
format32=$(od -An -tu1 -j20 -N2 "$out/format-48k-32f.wav" | tr -s ' ' | sed 's/^ //')
bits32=$(od -An -tu1 -j34 -N2 "$out/format-48k-32f.wav" | tr -s ' ' | sed 's/^ //')
test "$format32" = "3 0" && test "$bits32" = "32 0"
mkdir -p "$out/stems"
"$app/Contents/MacOS/Soundnet" --export-stems "$root/examples/audio-clip-compressor.json" "$out/stems" 1 48000 24 >/dev/null
stem_count=$(find "$out/stems" -name 'stem-*.wav' | wc -l | tr -d ' ')
test "$stem_count" -ge 1
# Historical examples remain useful smoke tests; early development does not
# require preserving their old sound or bit-identical migration output.
echo "== $count example graphs, MIDI/stem/format exports passed"
echo "== freshly built Audio Unit validation"
SOUNDNET_NODE_PATH="$component/Contents/Resources/Nodes" "$root/au/test.sh" "$component" "$out/au-tests"
echo "PASS: Soundnet $version ($build) release verification${2:+ (separately completed tests)}"
