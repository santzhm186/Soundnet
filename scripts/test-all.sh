#!/bin/bash
# Compile and run the complete Soundnet test suite against the current sources.
# Usage: scripts/test-all.sh [build-directory]
set -euo pipefail
export SOUNDNET_NO_AUTOSAVE=1
# Never race a running app's language preference while UI tests switch locales.
export SOUNDNET_PREFERENCES_SUITE="com.soundnet.tests.$$"
trap 'defaults delete "$SOUNDNET_PREFERENCES_SUITE" >/dev/null 2>&1 || true' EXIT
root="$(cd "$(dirname "$0")/.." && pwd)"
build="${1:-$root/build/tests}"
mkdir -p "$build"
cd "$root"

cxx=(xcrun clang++ -std=c++17 -O2 -mmacosx-version-min=13.0)
tests=(AbcScoreTests ArrangerTests AiTaskTests AudioFileTests AudioNodesTests ConcurrencyTests EngineTests RegistryTests NodeSpecTests InstanceGraphTests MusicNodesTests OfflineRenderTests RecorderTests SoakTests WorkflowSourcesTests)
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

echo "== compile InstanceEditorTest"
"${cxx[@]}" -fobjc-arc -I. tools/InstanceEditorTest.mm ui/InstanceEditor.mm src/WorkflowAssets.mm \
  -framework Cocoa -framework CoreML -framework UniformTypeIdentifiers -framework QuartzCore \
  -framework AVFoundation -o "$build/InstanceEditorTest"

echo "== compile ClipEditorTests"
"${cxx[@]}" -fobjc-arc -I. tests/ClipEditorTests.mm ui/InstanceEditor.mm src/WorkflowAssets.mm \
  -framework Cocoa -framework CoreML -framework UniformTypeIdentifiers -framework QuartzCore \
  -framework AVFoundation -o "$build/ClipEditorTests"

echo "== compile NodeLibraryTests"
"${cxx[@]}" -fobjc-arc -I. tests/NodeLibraryTests.mm ui/InstanceEditor.mm src/WorkflowAssets.mm \
  -framework Cocoa -framework CoreML -framework UniformTypeIdentifiers -framework QuartzCore \
  -framework AVFoundation -o "$build/NodeLibraryTests"

echo "== compile ProcessingUITests"
"${cxx[@]}" -fobjc-arc -I. tests/ProcessingUITests.mm ui/InstanceEditor.mm src/WorkflowAssets.mm \
  -framework Cocoa -framework CoreML -framework UniformTypeIdentifiers -framework QuartzCore \
  -framework AVFoundation -o "$build/ProcessingUITests"

echo "== compile LocalizationTests"
"${cxx[@]}" -fobjc-arc tests/LocalizationTests.mm -framework Foundation -o "$build/LocalizationTests"

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

echo "== run InstanceEditorTest"
mkdir -p "$root/build/qa"
SOUNDNET_NO_AUTOSAVE=1 "$build/InstanceEditorTest"

echo "== run ClipEditorTests"
"$build/ClipEditorTests"

echo "== run NodeLibraryTests"
"$build/NodeLibraryTests"

echo "== run ProcessingUITests"
"$build/ProcessingUITests"

echo "== run LocalizationTests"
"$build/LocalizationTests"

echo "PASS: all Soundnet tests"
