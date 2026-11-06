#!/bin/bash
set -euo pipefail
project="$(cd "$(dirname "$0")/.." && pwd)"
destination="${1:?Usage: build.sh destination-directory}"
app="$destination/Soundnet.app/Contents"
mkdir -p "$app/MacOS" "$app/Resources"
cp "$project/app/Info.plist" "$app/Info.plist"
if [[ -f "$project/ui/Ternary.icns" ]]; then cp "$project/ui/Ternary.icns" "$app/Resources/"; fi
xcrun clang++ -std=c++17 -O2 -fobjc-arc -fvisibility=hidden \
  -arch arm64 -arch x86_64 -mmacosx-version-min=13.0 \
  "$project/app/main.mm" "$project/app/TRIHost.mm" "$project/ui/TriEditor.mm" "$project/ui/WorkflowCanvas.mm" \
  -framework Cocoa -framework AudioToolbox -framework AVFoundation -framework AVFAudio \
  -framework CoreMIDI -framework QuartzCore -o "$app/MacOS/Soundnet"
codesign --force --sign - "$destination/Soundnet.app"
