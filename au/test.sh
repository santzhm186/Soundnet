#!/bin/bash
# Verify a specific build in this process. No installation or system AU lookup.
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
component="${1:?Usage: au/test.sh COMPONENT_PATH [OUTPUT_DIRECTORY]}"
component="$(cd "$component" && pwd -P)"
if [[ -n "${2:-}" ]]; then
  out="$2"
  mkdir -p "$out"
else
  out="$(mktemp -d "${TMPDIR:-/tmp}/soundnet-au-tests.XXXXXX")"
fi
out="$(cd "$out" && pwd -P)"
platform="$component/Contents/Frameworks/libSoundnetPlatform.dylib"
test -f "$platform"
test -f "$component/Contents/MacOS/Soundnet"
test -d "$component/Contents/Resources/Nodes"
xcrun clang++ -std=c++17 -O1 -fobjc-arc -DSOUNDNET_DYNAMIC_NODES=1 \
  "$root/au/verify.mm" "$platform" -Wl,-rpath,"$component/Contents/Frameworks" \
  -framework Cocoa -framework AudioToolbox -framework CoreMIDI \
  -o "$out/verify"
SOUNDNET_NODE_PATH="$component/Contents/Resources/Nodes" SOUNDNET_NO_AUTOSAVE=1 \
  "$out/verify" "$component" editor
