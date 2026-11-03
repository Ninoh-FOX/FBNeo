#!/bin/sh
# Builds the SDL2 standalone for the Miyoo Mini (Koriki) with LTO + PGO.
#   pgo/miyoomini/build.sh [use|generate|none] [WORKDIR]
#     use       LTO + the profile in pgo/miyoomini/profile (default)
#     generate  instrumented build: counters go to /mnt/SDCARD/pgo_tmp/prof on the device
#     none      LTO only
# The build runs in a copy of the tree (WORKDIR, default /tmp/fbneo-sdl2-miyoomini) and leaves
# the binary in pgo/miyoomini/fbneo.
# Objects are compiled as obj/..., so the counters are written as DIR/obj/.../x.gcda and
# -fprofile-use=DIR reads them back from the same place.
set -e
MODE=${1:-use}; W=${2:-/tmp/fbneo-sdl2-miyoomini}
HERE=$(cd "$(dirname "$0")" && pwd); ROOT=$(cd "$HERE/../.." && pwd)
TC=${MIYOOMINI_TOOLCHAIN:-/opt/miyoomini-toolchain}/bin/arm-linux-gnueabihf
J=$(nproc)
case $MODE in
  use) X="-fprofile-use=$HERE/profile -fprofile-correction -Wno-coverage-mismatch" ;;
  # -mlong-calls: the instrumented binary is so big that calls into libgcov fall out of BL range
  generate) X="-fprofile-generate=/mnt/SDCARD/pgo_tmp/prof -fprofile-update=prefer-atomic -mlong-calls" ;;
  none) X="" ;;
  *) echo "usage: $0 [use|generate|none] [WORKDIR]"; exit 1 ;;
esac
mkdir -p "$W"; rsync -a --delete --exclude .git --exclude /pgo --exclude /obj --exclude /fbneo "$ROOT/" "$W/"
cd "$W"
# make does not notice flag changes: start from clean objects when the mode changes
CCLINE="$TC-gcc -flto=$J -flto-partition=balanced $X -pthread"
if [ "$(cat .pgo_flags 2>/dev/null)" != "$CCLINE" ]; then rm -rf obj fbneo; echo "$CCLINE" > .pgo_flags; fi
LC_ALL=C make sdl2 MIYOO=1 MIYOO_LTO=1 \
  CC="$CCLINE" -j"$J"
$TC-strip -o "$HERE/fbneo" fbneo
ls -l "$HERE/fbneo"
