#!/bin/bash
# Compile and run the complete Soundnet test suite against the current sources.
# Usage: scripts/test-all.sh [build-directory]
set -euo pipefail
shopt -s nullglob
export PYTHONDONTWRITEBYTECODE=1
export SOUNDNET_NO_AUTOSAVE=1
# Never race a running app's language preference while UI tests switch locales.
export SOUNDNET_PREFERENCES_SUITE="com.soundnet.tests.$$"
trap 'defaults delete "$SOUNDNET_PREFERENCES_SUITE" >/dev/null 2>&1 || true' EXIT
root="$(cd "$(dirname "$0")/.." && pwd)"
build="${1:-$root/build/tests}"
mkdir -p "$build"
cd "$root"
python3 scripts/generate-node-registry.py
python3 -B tests/NodeRegistryGeneratorTests.py

cxx=(xcrun clang++ -std=c++17 -O2 -mmacosx-version-min=13.0)
tests=()
for source in tests/*Tests.cpp; do tests+=("$(basename "$source" .cpp)"); done
for name in "${tests[@]}"; do
  echo "== compile $name"
  "${cxx[@]}" "tests/$name.cpp" -o "$build/$name"
done

# A developer can add a complete node and its tests without editing this file.
node_tests=()
for source in nodes/*/Tests.cpp; do
  module=$(basename "$(dirname "$source")")
  [[ "$module" == _* ]] && continue
  name="node-$module"
  echo "== compile $name"
  "${cxx[@]}" "$source" -o "$build/$name"
  node_tests+=("$name")
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

# Node-specific editor regressions use the same public editor integration.
editor_tests=()
for source in tests/*EditorTests.mm; do
  name=$(basename "$source" .mm)
  echo "== compile $name"
  "${cxx[@]}" -fobjc-arc -I. "$source" ui/InstanceEditor.mm src/WorkflowAssets.mm \
    -framework Cocoa -framework CoreML -framework UniformTypeIdentifiers -framework QuartzCore \
    -framework AVFoundation -o "$build/$name"
  editor_tests+=("$name")
done

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

if [[ -f tests/EnvironmentUITests.mm ]]; then
  echo "== compile EnvironmentUITests"
  "${cxx[@]}" -fobjc-arc -I. tests/EnvironmentUITests.mm ui/InstanceEditor.mm src/WorkflowAssets.mm \
    -framework Cocoa -framework CoreML -framework UniformTypeIdentifiers -framework QuartzCore \
    -framework AVFoundation -o "$build/EnvironmentUITests"
fi

for name in "${tests[@]}"; do
  echo "== run $name"
  "$build/$name"
done
for name in "${node_tests[@]}"; do
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

for name in "${editor_tests[@]}"; do
  echo "== run $name"
  "$build/$name"
done

echo "== run NodeLibraryTests"
"$build/NodeLibraryTests"

echo "== run ProcessingUITests"
"$build/ProcessingUITests"

echo "== run LocalizationTests"
"$build/LocalizationTests"

if [[ -f tests/EnvironmentUITests.mm ]]; then
  echo "== run EnvironmentUITests"
  "$build/EnvironmentUITests"
fi

echo "== independent node package refresh"
"$root/scripts/test-node-packages.sh" "$build/node-packages"

echo "PASS: all Soundnet tests"
