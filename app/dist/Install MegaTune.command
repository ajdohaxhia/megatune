#!/bin/bash
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
DEST="$HOME/Library/Audio/Plug-Ins/VST3/MegaTune.vst3"
mkdir -p "$(dirname "$DEST")"
rm -rf "$DEST"
ditto "$HERE/MegaTune.vst3" "$DEST"
codesign --force --deep --sign - "$DEST" >/dev/null 2>&1 || true
echo "Installed: $DEST"
echo "Rescan plug-ins in your DAW."
