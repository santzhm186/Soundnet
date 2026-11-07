#!/bin/bash
# ThreadSanitizer check for the concurrent render/publish/MIDI paths.
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
build="${1:-$root/build/tests-tsan}"
mkdir -p "$build"
cxx=(xcrun clang++ -std=c++17 -O1 -g -fsanitize=thread -mmacosx-version-min=13.0)
for name in ConcurrencyTests SoakTests; do
  echo "== TSan compile $name"
  "${cxx[@]}" "$root/tests/$name.cpp" -o "$build/$name"
  echo "== TSan run $name"
  "$build/$name"
done
echo "PASS: thread sanitizer checks"
