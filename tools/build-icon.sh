#!/bin/bash
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
dest="${1:?icon output directory}"
mkdir -p "$dest"
xcrun clang "$root/tools/MakeIcon.m" -framework Foundation -framework CoreGraphics -framework ImageIO -o "$dest/icon-maker"
"$dest/icon-maker" "$dest/icon1024.png"
sips -z 512 512 "$dest/icon1024.png" --out "$dest/icon512.png" >/dev/null
sips -z 256 256 "$dest/icon1024.png" --out "$dest/icon256.png" >/dev/null
python3 - "$dest" <<'PY'
import pathlib,struct,sys
root=pathlib.Path(sys.argv[1])
entries=b''
for tag,name in [('ic10','icon1024.png'),('ic09','icon512.png'),('ic08','icon256.png')]:
    png=(root/name).read_bytes()
    entries+=tag.encode()+struct.pack('>I',len(png)+8)+png
(root/'Ternary.icns').write_bytes(b'icns'+struct.pack('>I',len(entries)+8)+entries)
PY
