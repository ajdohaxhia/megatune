#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")"
export DEVELOPER_DIR=/Library/Developer/CommandLineTools
cmake -S . -B build-cli -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER="$DEVELOPER_DIR/usr/bin/clang" -DCMAKE_CXX_COMPILER="$DEVELOPER_DIR/usr/bin/clang++" "-DJUCE_DIR=${JUCE_DIR:-/private/tmp/juce}" "-DCMAKE_OSX_ARCHITECTURES=${OSX_ARCHITECTURES:-arm64;x86_64}"
cmake --build build-cli -j 6
(cd build-cli && ctest --output-on-failure)
if [ -d ../MegaTune.vst3 ]; then mv ../MegaTune.vst3 "../MegaTune.previous-$(date +%Y%m%d-%H%M%S).vst3"; fi
ditto build-cli/MegaTune_artefacts/Release/VST3/MegaTune.vst3 ../MegaTune.vst3
# The VST3 helper signs the bundle before moduleinfo.json is generated, which
# leaves the seal stale: dyld then refuses to load it ("library load disallowed
# by system policy"). Re-seal the finished bundle, then verify and load-test it.
codesign --force --deep --sign - ../MegaTune.vst3
codesign --verify --deep --strict ../MegaTune.vst3
if [ -x "host/build/MegaTuneHostCheck_artefacts/Release/MegaTuneHostCheck" ]; then
  ok=0
  for attempt in 1 2 3; do
    if host/build/MegaTuneHostCheck_artefacts/Release/MegaTuneHostCheck ../MegaTune.vst3; then ok=1; break; fi
    sleep 1
  done
  [ "$ok" -eq 1 ]
fi
