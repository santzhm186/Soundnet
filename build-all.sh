#!/bin/bash
set -euo pipefail
root="$(cd "$(dirname "$0")" && pwd)"
release="${1:-$root/build/release}"
"$root/app/build.sh" "$release"
"$root/au/build.sh" "$release" "$root/build/sdk"
