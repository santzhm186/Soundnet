#!/bin/bash
# Install the built Soundnet Audio Unit into the user's Audio Components folder.
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
component="${1:-}"
if [ -z "$component" ]; then
    component="$(ls -d "$root"/build/*/Soundnet.component 2>/dev/null | sort | tail -1 || true)"
fi
if [ -z "$component" ] || [ ! -d "$component" ]; then
    echo "用法: $0 path/to/Soundnet.component" >&2
    echo "先运行 ./build-all.sh build/release" >&2
    exit 1
fi
component="$(cd "$component" && pwd)"
codesign --verify --deep --strict "$component" >/dev/null
dest="$HOME/Library/Audio/Plug-Ins/Components"
mkdir -p "$dest"
rm -rf "$dest/Soundnet.component"
cp -R "$component" "$dest/Soundnet.component"
echo "已安装: $dest/Soundnet.component"
echo "版本: $(/usr/libexec/PlistBuddy -c 'Print :CFBundleShortVersionString' "$dest/Soundnet.component/Contents/Info.plist")"
echo "正在验证 Audio Unit…"
auval -v aumu Tern Trny
