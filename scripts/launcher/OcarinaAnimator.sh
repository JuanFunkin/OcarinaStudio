#!/bin/sh
# OcarinaAnimator launcher (Linux): starts OcarinaStudio in Animator mode.
# Keep it in the same folder as OcarinaStudio.appimage.
here="$(cd "$(dirname "$0")" && pwd)"
cd "$here" || exit 1
OCARINA_ANIMATOR=1 exec "$here/OcarinaStudio.appimage" "$@"
