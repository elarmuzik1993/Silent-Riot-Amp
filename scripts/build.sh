#!/usr/bin/env bash
set -euo pipefail
TYPE="${1:-Debug}"
DIR="build"
[[ "$TYPE" =~ [Rr]elease ]] && TYPE="Release" && DIR="build-release"
echo "SilentRiot — $TYPE build"
cmake -B "$DIR" -G Ninja -DCMAKE_BUILD_TYPE="$TYPE" -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build "$DIR" -j"$(nproc)"
[ -f "$DIR/compile_commands.json" ] && ln -sf "$DIR/compile_commands.json" compile_commands.json
echo "Done! VST3: $DIR/SilentRiot_artefacts/$TYPE/VST3/"
