#!/bin/bash
set -euo pipefail
project="$(cd "$(dirname "$0")/.." && pwd)"
python3 "$project/scripts/generate-node-registry.py"
destination="${1:?Usage: build.sh destination-directory}"
app="$destination/Soundnet.app/Contents"
mkdir -p "$app/MacOS" "$app/Resources/Nodes" "$app/Frameworks"
cp "$project/app/Info.plist" "$app/Info.plist"
cp "$project/LICENSE" "$project/NOTICE" "$app/Resources/"
rm -f "$app/Resources/"{refresh-app-nodes.py,node-source-root.txt,node-build-fingerprint.txt}
"$project/scripts/build-platform.sh" "$app/Frameworks"
node_arch=(--arch arm64 --arch x86_64);arch=(-arch arm64 -arch x86_64)
if [[ -n "${SOUNDNET_BUILD_ARCH:-}" ]]; then arch=(-arch "$SOUNDNET_BUILD_ARCH");node_arch=(--arch "$SOUNDNET_BUILD_ARCH");fi
python3 "$project/scripts/build-node.py" all "$app/Resources/Nodes" --platform-library "$app/Frameworks/libSoundnetPlatform.dylib" "${node_arch[@]}"
if [[ -f "$project/ui/Ternary.icns" ]]; then cp "$project/ui/Ternary.icns" "$app/Resources/";
else "$project/tools/build-icon.sh" "$destination/icon-build";cp "$destination/icon-build/Ternary.icns" "$app/Resources/";fi
xcrun clang++ -std=c++17 -O2 -fobjc-arc -fvisibility=hidden -DSOUNDNET_DYNAMIC_NODES=1 \
  "${arch[@]}" -mmacosx-version-min=13.0 \
  "$project/app/main.mm" "$project/app/TRIHost.mm" "$project/ui/InstanceEditor.mm" \
  "$app/Frameworks/libSoundnetPlatform.dylib" -Wl,-rpath,@executable_path/../Frameworks \
  -framework Cocoa -framework AudioToolbox -framework AVFoundation -framework AVFAudio \
  -framework CoreMIDI -framework CoreAudio -framework CoreML -framework UniformTypeIdentifiers -framework QuartzCore -o "$app/MacOS/Soundnet"
codesign --force --sign - "$destination/Soundnet.app"
