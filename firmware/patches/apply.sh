#!/usr/bin/env bash
# Apply the Blupuck patches on top of the pinned upstream bluepad32 submodule.
# Idempotent: skips cleanly if the patch is already applied.
#
# Run after `git submodule update --init --recursive`, both locally and in CI.
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
submodule="$repo_root/firmware/lib/bluepad32"
patch="$repo_root/firmware/patches/bluepad32-blupuck.patch"

cd "$submodule"

if git apply --reverse --check "$patch" >/dev/null 2>&1; then
    echo "bluepad32 patch already applied; nothing to do."
    exit 0
fi

git apply --check "$patch"
git apply "$patch"
echo "bluepad32 patch applied."
