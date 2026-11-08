#!/bin/bash
# Compile and run the complete Soundnet test suite against the current sources.
# Usage: scripts/test-all.sh [build-directory]
set -euo pipefail
export SOUNDNET_NO_AUTOSAVE=1
root="$(cd "$(dirname "$0")/.." && pwd)"
build="${1:-$root/build/tests}"
mkdir -p "$build"
cd "$root"

cxx=(xcrun clang++ -std=c++17 -O2 -mmacosx-version-min=13.0)
tests=(ArrangerTests AudioFileTests AudioNodesTests ConcurrencyTests EngineTests RegistryTests NodeSpecTests InstanceGraphTests MusicNodesTests OfflineRenderTests RecorderTests SoakTests WorkflowSourcesTests)
for name in "${tests[@]}"; do
  echo "== compile $name"
  "${cxx[@]}" "tests/$name.cpp" -o "$build/$name"
done

echo "== compile RenderAudition"
"${cxx[@]}" tests/RenderAudition.cpp -o "$build/RenderAudition"

echo "== compile AssetImportTests"
"${cxx[@]}" -fobjc-arc tests/AssetImportTests.mm src/WorkflowAssets.mm \
  -framework Foundation -framework CoreML -framework AVFoundation -o "$build/AssetImportTests"

echo "== compile AssetStoreTests"
"${cxx[@]}" -fobjc-arc -I. tests/AssetStoreTests.mm src/WorkflowAssets.mm \
  -framework Foundation -framework CoreML -framework AVFoundation -o "$build/AssetStoreTests"

echo "== compile WorkflowFormatTests"
"${cxx[@]}" -fobjc-arc -I. tests/WorkflowFormatTests.mm src/WorkflowAssets.mm \
  -framework Foundation -framework CoreML -framework AVFoundation -o "$build/WorkflowFormatTests"

echo "== compile ClipEditorTests"
"${cxx[@]}" -fobjc-arc -I. tests/ClipEditorTests.mm ui/InstanceEditor.mm src/WorkflowAssets.mm \
  -framework Cocoa -framework CoreML -framework UniformTypeIdentifiers -framework QuartzCore \
  -framework AVFoundation -o "$build/ClipEditorTests"

for name in "${tests[@]}"; do
  echo "== run $name"
  "$build/$name"
done

echo "== run RenderAudition"
"$build/RenderAudition" "$build/render-audition.wav"
test -s "$build/render-audition.wav"

echo "== run AssetImportTests"
"$build/AssetImportTests"

echo "== run AssetStoreTests"
"$build/AssetStoreTests"

echo "== run WorkflowFormatTests"
"$build/WorkflowFormatTests"

echo "== run ClipEditorTests"
"$build/ClipEditorTests"

echo "PASS: all Soundnet tests"
