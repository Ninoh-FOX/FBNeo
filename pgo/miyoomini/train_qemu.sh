#!/bin/bash
# Records the PGO profile on the PC: runs the instrumented ARM binary under qemu-arm with SDL's
# dummy video/audio drivers (attract mode, no input) and leaves the counters in ./gcov.
#   pgo/miyoomini/build.sh generate
#   pgo/miyoomini/train_qemu.sh ROMDIR [seconds per game]
#   rm -rf pgo/miyoomini/profile; cp -a pgo/miyoomini/gcov/mnt/SDCARD/pgo_tmp/prof pgo/miyoomini/profile
#   python3 pgo/miyoomini/prune_gcda.py pgo/miyoomini/profile; pgo/miyoomini/build.sh use
# The SDL2 of Koriki only has the Miyoo video driver, so the 2.0.16 build of the toolchain sysroot
# (it has "dummy") is used instead. The Miyoo framebuffer code fails softly (640x480 defaults).
set -e
HERE=$(cd "$(dirname "$0")" && pwd); ROMS=${1:?usage: $0 ROMDIR [seconds]}; SECS=${2:-150}
SR=${MIYOOMINI_TOOLCHAIN:-/opt/miyoomini-toolchain}/arm-buildroot-linux-gnueabihf/sysroot
W=$HERE/qemu_run; rm -rf "$W" "$HERE/gcov"; mkdir -p "$W/lib"
cp "$SR/usr/lib/libSDL2-2.0.so.0.16.0" "$W/lib/libSDL2-2.0.so.0"
cp "$HERE/fbneo" "$W/fbneo"; cd "$W"
export QEMU_LD_PREFIX=$SR LD_LIBRARY_PATH=$W/lib SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy
export GCOV_PREFIX=$HERE/gcov GCOV_PREFIX_STRIP=0
timeout -s TERM 20 qemu-arm ./fbneo galaga > /dev/null 2>&1 || true	# writes the default fbneo.ini
sed -i "s|^szAppRomPaths\[0\].*|szAppRomPaths[0] $ROMS/|" fbneo.ini
# game, scanlines, Scale2x, full screen, seconds (CPS3 needs ~3000 frames to boot)
while read g sl s2 fs secs; do
	sed -i "s/^bVidScanlines .*/bVidScanlines $sl/; s/^bVidScale2x .*/bVidScale2x $s2/; s/^bVidFullStretch .*/bVidFullStretch $fs/" fbneo.ini
	timeout -s TERM ${secs:-$SECS} qemu-arm ./fbneo $g > log_$g.txt 2>&1 || true
	echo "$g: $(grep -a 'frames per second' log_$g.txt | tr -s ' ')"
done <<LIST
sfiii3 0 0 0 400
sfa3 1 0 0
ssf2t 0 1 0
sf2ce 1 1 0
ffight 0 0 1
kof98 1 0 1
mslug 0 1 1
ddonpach 1 1 0
outrun 0 0 0
LIST
echo "gcda: $(find "$HERE/gcov" -name '*.gcda' | wc -l)"
