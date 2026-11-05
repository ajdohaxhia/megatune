#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")"
source_plugin="$PWD/MegaTune.vst3"
target_dir="$HOME/Library/Audio/Plug-Ins/VST3"
if [ ! -d "$source_plugin" ]; then
  echo "MegaTune.vst3 is missing beside this installer."
  exit 1
fi
mkdir -p "$target_dir"
if [ -e "$target_dir/MegaTune.vst3" ]; then
  backup="$target_dir/MegaTune.vst3.backup-$(date +%Y%m%d-%H%M%S)"
  mv "$target_dir/MegaTune.vst3" "$backup"
fi
/usr/bin/ditto "$source_plugin" "$target_dir/MegaTune.vst3"
echo "MegaTune installed. Restart your DAW and rescan VST3 plug-ins."
