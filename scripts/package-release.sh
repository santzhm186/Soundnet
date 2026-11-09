#!/bin/bash
# Package a built app + AU + examples + docs into a zip for distribution.
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
build="${1:-}"
out="${2:-}"
if [ -z "$build" ] || [ ! -d "$build/Soundnet.app" ] || [ ! -d "$build/Soundnet.component" ]; then
    echo "用法: $0 build/243-final [output.zip]" >&2
    exit 1
fi
version="$(/usr/libexec/PlistBuddy -c 'Print :CFBundleShortVersionString' "$build/Soundnet.app/Contents/Info.plist")"
out="${out:-$root/build/Soundnet-$version-macos.zip}"
mkdir -p "$(dirname "$out")"
out="$(cd "$(dirname "$out")" && pwd)/$(basename "$out")"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT
name="Soundnet-$version"
mkdir -p "$tmp/$name"
cp -R "$build/Soundnet.app" "$build/Soundnet.component" "$tmp/$name/"
cp "$root/README.md" "$root/README.zh-CN.md" "$root/CHANGELOG.md" "$root/AGENTS.md" "$root/LICENSE" "$root/NOTICE" "$tmp/$name/"
cp -R "$root/examples" "$root/docs" "$root/scripts" "$tmp/$name/"
(cd "$tmp" && ditto -c -k --sequesterRsrc --keepParent "$name" "$out")
echo "已打包: $out"
