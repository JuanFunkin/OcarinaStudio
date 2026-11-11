#!/usr/bin/env bash
# Applies OcarinaStudio on top of a Ship of Harkinian (Shipwright) checkout.
#
#   scripts/apply.sh <path-to-shipwright-checkout>
#
# 1. patches/*.patch   - the few engine hooks (HUD, camera, background, window registration)
# 2. overlay/          - the OcarinaStudio module (soh/soh/OcarinaStudio/*), copied in as new files
# 3. assets/icons      - replaces the Ship of Harkinian application icons
set -euo pipefail

if [ $# -ne 1 ]; then
    echo "usage: $0 <path-to-shipwright-checkout>" >&2
    exit 2
fi

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
UP="$(cd "$1" && pwd)"

if [ ! -f "$UP/soh/CMakeLists.txt" ]; then
    echo "error: $UP does not look like a Shipwright checkout" >&2
    exit 1
fi

echo "==> Applying engine patches"
for patch in "$HERE"/patches/*.patch; do
    echo "    $(basename "$patch")"
    git -C "$UP" apply --ignore-whitespace --check "$patch"
    git -C "$UP" apply --ignore-whitespace "$patch"
done

echo "==> Copying the OcarinaStudio module"
cp -r "$HERE/overlay/." "$UP/"

echo "==> Replacing application icons"
cp "$HERE/assets/icons/icon.ico"      "$UP/soh/SHIPOFHARKINIAN.ico"
cp "$HERE/assets/icons/icon-1024.png" "$UP/soh/macosx/sohIcon.png"
cp "$HERE/assets/icons/icon-256.jpg"  "$UP/soh/icon.jpg"

echo "==> Done"
