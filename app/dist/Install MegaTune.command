#!/bin/bash
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
VST3="$HOME/Library/Audio/Plug-Ins/VST3/MegaTune.vst3"
AU="$HOME/Library/Audio/Plug-Ins/Components/MegaTune.component"
mkdir -p "$(dirname "$VST3")" "$(dirname "$AU")"
if [ -d "$HERE/MegaTune.vst3" ]; then
  rm -rf "$VST3"
  ditto "$HERE/MegaTune.vst3" "$VST3"
  find "$VST3" -exec xattr -c {} \; 2>/dev/null || true
  codesign --force --deep --sign - "$VST3" >/dev/null 2>&1 || true
  echo "Installed VST3: $VST3"
fi
if [ -d "$HERE/MegaTune.component" ]; then
  rm -rf "$AU"
  ditto "$HERE/MegaTune.component" "$AU"
  find "$AU" -exec xattr -c {} \; 2>/dev/null || true
  xattr -c "$AU"
  xattr -d com.apple.FinderInfo "$AU" 2>/dev/null || true
  xattr -d 'com.apple.fileprovider.fpfs#P' "$AU" 2>/dev/null || true
  codesign --force --deep --sign - "$AU" >/dev/null 2>&1 || true
  echo "Installed AU: $AU"
fi
echo "Rescan plug-ins in your DAW."
