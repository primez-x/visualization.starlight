#!/bin/sh
# Build the CoreELEC ARM module and stage the installable add-on into a checkout of the armv7 branch.
# Usage: tools/stage_armv7.sh <armv7-worktree>
set -eu
dest=${1:?usage: tools/stage_armv7.sh <armv7-worktree>}
root=$(cd "$(dirname "$0")/.." && pwd)
build=${STARLIGHT_BUILD_DIR:-/tmp/visualization.starlight-armv7}
rm -rf "$build"
cmake -S "$root" -B "$build/cmake" -DCMAKE_TOOLCHAIN_FILE="$root/cmake/coreelec-arm.cmake" \
  -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF
cmake --build "$build/cmake" -j2
cmake --install "$build/cmake" --prefix "$build/package" --strip
rm -rf "$dest/resources" "$dest/visualization.starlight.so" "$dest/addon.xml"
cp -R "$build/package/visualization.starlight/." "$dest/"
echo "Staged $(git -C "$root" rev-parse HEAD) into $dest"
