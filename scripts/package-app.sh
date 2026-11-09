#!/bin/bash
# Package the standalone app as a local macOS Installer package.
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
build="${1:-$root/build/release}"
app="$build/Soundnet.app"
if [ ! -d "$app" ]; then
    echo "Usage: $0 [build-directory] [output.pkg]" >&2
    exit 1
fi
codesign --verify --deep --strict "$app"
plist="$app/Contents/Info.plist"
version="$(/usr/libexec/PlistBuddy -c 'Print :CFBundleShortVersionString' "$plist")"
identifier="$(/usr/libexec/PlistBuddy -c 'Print :CFBundleIdentifier' "$plist")"
out="${2:-$root/build/Soundnet-$version-macos.pkg}"
mkdir -p "$(dirname "$out")"

stage="$(mktemp -d)"
trap 'rm -rf "$stage"' EXIT
mkdir "$stage/payload"
ditto "$app" "$stage/payload/Soundnet.app"
pkgbuild --analyze --root "$stage/payload" "$stage/components.plist"
# Always install in /Applications, even if a build or backup exists elsewhere.
/usr/libexec/PlistBuddy -c 'Set :0:BundleIsRelocatable false' "$stage/components.plist"
pkgbuild --root "$stage/payload" --component-plist "$stage/components.plist" \
    --identifier "$identifier" --version "$version" --install-location /Applications "$out"
