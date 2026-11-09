#!/bin/bash
# Run an isolated module test without compiling other node implementations.
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
module="${1:?Usage: scripts/test-node.sh node-directory-name [output-directory]}"
[[ "$module" =~ ^[a-z][a-z0-9_-]*$ ]] || { echo "Invalid node directory name" >&2; exit 1; }
source="$root/nodes/$module/Tests.cpp"
[[ -f "$source" ]] || { echo "Node has no independent Tests.cpp: $module" >&2; exit 1; }
out="${2:-$root/build/node-tests/$module}"
mkdir -p "$out"
xcrun clang++ -std=c++17 -O2 -mmacosx-version-min=13.0 "$source" -o "$out/Tests"
"$out/Tests"
