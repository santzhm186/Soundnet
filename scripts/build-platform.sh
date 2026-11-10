#!/bin/bash
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
out="${1:?Usage: build-platform.sh output-directory}"
mkdir -p "$out"
arch=(-arch arm64 -arch x86_64)
if [[ -n "${SOUNDNET_BUILD_ARCH:-}" ]]; then arch=(-arch "$SOUNDNET_BUILD_ARCH"); fi
xcrun clang++ -std=c++17 -O2 -fobjc-arc -DSOUNDNET_DYNAMIC_NODES=1 -dynamiclib \
  "${arch[@]}" -mmacosx-version-min=13.0 \
  "$root/platform/NodeCatalog.cpp" "$root/platform/NodePackageLoader.mm" "$root/src/WorkflowAssets.mm" \
  -framework Foundation -framework CoreML -framework AVFoundation \
  -install_name @rpath/libSoundnetPlatform.dylib -o "$out/libSoundnetPlatform.dylib"
codesign --force --sign - "$out/libSoundnetPlatform.dylib"
