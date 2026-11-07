#!/usr/bin/env bash
# Run the DSP-heavy C++ tests under AddressSanitizer and UndefinedBehaviorSanitizer.
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
out="${1:-$root/build/sanitize}"
mkdir -p "$out"
flags=(-std=c++17 -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer -mmacosx-version-min=13.0)
for test in AudioNodesTests AudioFileTests ParserFuzzTests ConcurrencyTests SoakTests; do
    echo "== $test"
    xcrun clang++ "${flags[@]}" "$root/tests/$test.cpp" -o "$out/$test"
    # Engine state is large; ASan needs a larger stack than the default.
    (ulimit -s 32768; ASAN_OPTIONS=detect_leaks=0 "$out/$test")
done
echo "PASS: sanitizer checks"
