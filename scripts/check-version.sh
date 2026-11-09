#!/bin/bash
# Verify that every user-visible version string and the AU component version agree.
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
app="$root/app/Info.plist"
au="$root/au/Info.plist"
view="$root/au/ViewInfo.plist"
get() { /usr/libexec/PlistBuddy -c "Print :$2" "$1"; }
short=$(get "$app" CFBundleShortVersionString)
build=$(get "$app" CFBundleVersion)
for pair in "$au:au" "$view:view"; do
    file="${pair%%:*}"; label="${pair##*:}"
    [ "$(get "$file" CFBundleShortVersionString)" = "$short" ] || { echo "ERROR: $label short version mismatch" >&2; exit 1; }
    [ "$(get "$file" CFBundleVersion)" = "$build" ] || { echo "ERROR: $label build mismatch" >&2; exit 1; }
done
[[ "$short" =~ ^([0-9]+)\.([0-9]+)\.([0-9]+)$ ]] || { echo "ERROR: bad version $short" >&2; exit 1; }
major=${BASH_REMATCH[1]}; minor=${BASH_REMATCH[2]}; patch=${BASH_REMATCH[3]}
expected_au=$(( (major << 16) | (minor << 8) | patch ))
actual_au=$(get "$au" "AudioComponents:0:version")
[ "$actual_au" = "$expected_au" ] || { echo "ERROR: AudioComponents version $actual_au != $expected_au" >&2; exit 1; }
hex=$(printf '0x%05X' "$expected_au")
grep -Fq "$hex" "$root/au/verify.mm" || { echo "ERROR: au/verify.mm lacks $hex" >&2; exit 1; }
grep -Fq "**$major.$minor**" "$root/README.md" || { echo "ERROR: README lacks $major.$minor" >&2; exit 1; }
grep -Fq "**$major.$minor**" "$root/README.zh-CN.md" || { echo "ERROR: Chinese README lacks $major.$minor" >&2; exit 1; }
grep -Fq "Soundnet $major.$minor ·" "$root/docs/使用说明.txt" || { echo "ERROR: manual title lacks $major.$minor" >&2; exit 1; }
grep -Fq "Soundnet $major.$minor" "$root/ui/InstanceEditor.mm" || { echo "ERROR: editor heading lacks $major.$minor" >&2; exit 1; }
grep -Fq "## $major.$minor" "$root/CHANGELOG.md" || { echo "ERROR: CHANGELOG lacks $major.$minor" >&2; exit 1; }
echo "PASS: version $short ($build), AU $hex"
