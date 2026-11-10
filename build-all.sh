#!/bin/bash
set -euo pipefail
root="$(cd "$(dirname "$0")" && pwd)"
release="${1:-$root/build/release}"
"$root/app/build.sh" "$release"
"$root/au/build.sh" "$release" "$release/sdk"
"$root/scripts/export-node-sdk.sh" "$release/Soundnet.app/Contents/Frameworks/libSoundnetPlatform.dylib" "$release/Soundnet-Node-SDK"
