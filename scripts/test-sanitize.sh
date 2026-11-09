#!/usr/bin/env bash
# Run the DSP-heavy C++ tests under AddressSanitizer and UndefinedBehaviorSanitizer.
set -euo pipefail
shopt -s nullglob
root="$(cd "$(dirname "$0")/.." && pwd)"
python3 "$root/scripts/generate-node-registry.py"
out="${1:-$root/build/sanitize}"
mkdir -p "$out"
flags=(-std=c++17 -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer -mmacosx-version-min=13.0)
for test in AbcScoreTests AiTaskTests AudioNodesTests AudioFileTests InstanceGraphTests NodeSpecTests NodeModuleTests TaskServiceTests ProjectEnvironmentTests KernelIntegrationTests MusicNodesTests OfflineRenderTests ParserFuzzTests ConcurrencyTests SoakTests; do
    [[ -f "$root/tests/$test.cpp" ]] || continue
    echo "== $test"
    xcrun clang++ "${flags[@]}" "$root/tests/$test.cpp" -o "$out/$test"
    # Engine state is large; ASan needs a larger stack than the default.
    (ulimit -s 32768; ASAN_OPTIONS=detect_leaks=0 "$out/$test")
done
for source in "$root"/nodes/*/Tests.cpp; do
    module=$(basename "$(dirname "$source")")
    [[ "$module" == _* ]] && continue
    echo "== node-$module"
    xcrun clang++ "${flags[@]}" "$source" -o "$out/node-$module"
    (ulimit -s 32768; ASAN_OPTIONS=detect_leaks=0 "$out/node-$module")
done
echo "PASS: sanitizer checks"
