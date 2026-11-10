#!/bin/bash
# Export only the public SDK and an existing platform library. No host or node
# implementation is required to build an independently maintained node.
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
library="${1:?Usage: export-node-sdk.sh platform-library output-directory}"
out="${2:?Usage: export-node-sdk.sh platform-library output-directory}"
test -f "$library"
mkdir -p "$out"
python3 - "$root" "$library" "$out" <<'PY'
import json, shutil, sys
from pathlib import Path
root, library, out = map(lambda value: Path(value).resolve(), sys.argv[1:])
if out == root or root in out.parents and out.parts[len(root.parts)] in ('platform', 'nodes', 'scripts'):
    raise SystemExit('SDK output must not overwrite the source tree')
for relative in ('lib', 'scripts', 'docs', 'platform'):
    (out / relative).mkdir(parents=True, exist_ok=True)
for source in (root / 'platform').rglob('*'):
    if source.is_file() and source.suffix in ('.h', '.hpp', '.inc'):
        target = out / source.relative_to(root)
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, target)
shutil.copy2(library, out / 'lib/libSoundnetPlatform.dylib')
shutil.copy2(root / 'scripts/build-node.py', out / 'scripts/build-node.py')
for name in ('项目宪法.md', '编写新节点.md'):
    shutil.copy2(root / 'docs' / name, out / 'docs' / name)
for name in ('LICENSE', 'NOTICE'):
    shutil.copy2(root / name, out / name)
(out / 'sdk.json').write_text(json.dumps({'platformVersion': '3.0.0', 'sdkVersion': 2, 'packageABI': 2}, indent=2) + '\n')
(out / 'README.txt').write_text('''Soundnet 3.0.0 — Node SDK 2 / Package ABI 2

This SDK contains public headers, the platform link library, and the independent
node builder. It does not contain the host or any built-in node implementation.

Build a node (replace /path/to/my-effect with your own source directory):
  python3 scripts/build-node.py /path/to/my-effect /path/to/output \\
    --platform-library lib/libSoundnetPlatform.dylib

Run this command from the SDK directory. The builder finds this SDK automatically.
Use the same Xcode/clang major version as the target platform; the package loader
checks ABI, SDK and layout compatibility. Default output is arm64 + x86_64.
See docs/编写新节点.md and docs/项目宪法.md for the complete interface contract.

Copy the resulting .soundnode directory into:
  ~/Library/Application Support/Soundnet/Nodes
Then choose Refresh in the Soundnet node library.
''')
print('Exported public node SDK: ' + str(out))
PY
