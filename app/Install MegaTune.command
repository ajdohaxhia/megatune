#!/bin/bash
# Installs MegaTune (VST3 and AU) for the current user. Any existing copy is
# moved to the Trash first.
set -euo pipefail
cd "$(dirname "$0")"
installed=0
install_bundle() {
  local bundle="$1" target_dir="$2"
  [ -d "$PWD/$bundle" ] || return 0
  mkdir -p "$target_dir"
  if [ -e "$target_dir/$bundle" ]; then
    mv "$target_dir/$bundle" "$HOME/.Trash/$bundle.replaced-$(date +%Y%m%d-%H%M%S)"
  fi
  /usr/bin/ditto "$PWD/$bundle" "$target_dir/$bundle"
  /usr/bin/xattr -dr com.apple.quarantine "$target_dir/$bundle" 2>/dev/null || true
  echo "Installed $bundle to $target_dir"
  installed=1
}
install_bundle MegaTune.vst3 "$HOME/Library/Audio/Plug-Ins/VST3"
install_bundle MegaTune.component "$HOME/Library/Audio/Plug-Ins/Components"
if [ "$installed" -eq 0 ]; then
  echo "MegaTune.vst3 and MegaTune.component are missing beside this installer."
  exit 1
fi
echo "Done. Restart your DAW and rescan plug-ins."
