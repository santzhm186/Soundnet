#!/bin/bash
# build.sh OUTPUT_DIRECTORY [INTERMEDIATE_DIRECTORY]
set -euo pipefail
src="$(cd "$(dirname "$0")" && pwd)"
python3 "$src/../scripts/generate-node-registry.py"
out="${1:-$src/../build-au}"
obj="${2:-$src/../build-au/sdk}"
mkdir -p "$out" "$obj"
bundle="$out/Soundnet.component/Contents"
view="$bundle/Resources/TernaryView.bundle/Contents"
mkdir -p "$bundle/MacOS" "$view/MacOS" "$bundle/Frameworks" "$bundle/Resources/Nodes"
"$src/../scripts/build-platform.sh" "$bundle/Frameworks"
node_arch=(--arch arm64 --arch x86_64)
if [[ -n "${SOUNDNET_BUILD_ARCH:-}" ]]; then node_arch=(--arch "$SOUNDNET_BUILD_ARCH");fi
python3 "$src/../scripts/build-node.py" all "$bundle/Resources/Nodes" --platform-library "$bundle/Frameworks/libSoundnetPlatform.dylib" "${node_arch[@]}"
common=(-arch arm64 -arch x86_64 -mmacosx-version-min=13.0 -O2 -DNDEBUG -DSOUNDNET_DYNAMIC_NODES=1)
if [[ -n "${SOUNDNET_BUILD_ARCH:-}" ]]; then common=(-arch "$SOUNDNET_BUILD_ARCH" -mmacosx-version-min=13.0 -O2 -DNDEBUG -DSOUNDNET_DYNAMIC_NODES=1);fi
auflags=(-std=c++23 -fvisibility=hidden -DAUSDK_HAVE_MIDI2=0 "-I$src/vendor/include")
objects=()
for path in "$src"/vendor/src/AudioUnitSDK/*.cpp; do
  object="$obj/$(basename "$path" .cpp).o"
  if [[ ! -f "$object" || "$path" -nt "$object" || "$src/build.sh" -nt "$object" ]]; then
    xcrun clang++ "${common[@]}" "${auflags[@]}" -c "$path" -o "$object"
  fi
  objects+=("$object")
done
xcrun clang++ "${common[@]}" "${auflags[@]}" -fobjc-arc "$src/TernaryAU.mm" "$bundle/Frameworks/libSoundnetPlatform.dylib" -Wl,-rpath,@loader_path/../Frameworks "${objects[@]}" -bundle -framework AudioUnit -framework AudioToolbox -framework AVFoundation -framework CoreFoundation -framework CoreAudio -framework CoreMIDI -framework Foundation -framework CoreML -o "$bundle/MacOS/Soundnet"
cp "$src/Info.plist" "$bundle/Info.plist"
cp "$src/../LICENSE" "$src/../NOTICE" "$bundle/Resources/"
cp "$src/vendor/LICENSE.txt" "$bundle/Resources/AudioUnitSDK-LICENSE.txt"
xcrun clang++ "${common[@]}" -std=c++17 -fobjc-arc -DSNInstanceEditor=SNAUInstanceEditor -DSNInstanceCanvas=SNAUInstanceCanvas -DSNPiano=SNAUPiano -DSNFlippedView=SNAUFlippedView -DTRNKnob=TRNAUKnob -DTRNKeys=TRNAUKeys -DTRNEditor=TRNAUEditor -DTRIWorkflowCanvas=TRIAUWorkflowCanvas "$src/EditorFactory.mm" "$bundle/Frameworks/libSoundnetPlatform.dylib" -Wl,-rpath,@loader_path/../../../../Frameworks "$src/../ui/InstanceEditor.mm" -bundle -framework Cocoa -framework UniformTypeIdentifiers -framework QuartzCore -framework CoreML -framework AudioToolbox -framework AVFoundation -o "$view/MacOS/TernaryView"
cp "$src/ViewInfo.plist" "$view/Info.plist"
/usr/bin/codesign --force --sign - "$view/.."
/usr/bin/codesign --force --sign - "$bundle/.."
/usr/bin/codesign --verify --deep --strict "$bundle/.."
