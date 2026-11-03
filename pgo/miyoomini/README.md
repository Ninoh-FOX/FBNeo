# Miyoo Mini build (Koriki), SDL2 standalone: LTO + PGO

    pgo/miyoomini/build.sh            # -> pgo/miyoomini/fbneo (LTO + the profile in profile/)
    pgo/miyoomini/build.sh none       # same flags, no profile
    pgo/miyoomini/build.sh generate   # instrumented build for recording the profile

`build.sh` builds in a copy of the tree (default `/tmp/fbneo-sdl2-miyoomini`) with
`make sdl2 MIYOO=1 MIYOO_LTO=1` and the miyoomini toolchain (GCC 8.3):

- `MIYOO_LTO=1` drops the explicit `-march=armv7ve` (with it `lto1` segfaults; the
  toolchain default `-mcpu=cortex-a7` is the same architecture);
- `lib7z/7zCrc.c` is built without LTO: its ARMv8 CRC path (chosen at run time,
  never used on the Cortex-A7) does not build inside LTO with profiling;
- the instrumented build uses `-mlong-calls`: it is ~97 MB and the calls into
  libgcov fall out of `BL` range otherwise (only for recording, not in the final build);
- objects are rebuilt from scratch when the mode changes (make does not notice flag changes).

## Profile

`profile/` was recorded on the PC with `train_qemu.sh`: the instrumented ARM binary runs
under `qemu-arm` with SDL's dummy video/audio drivers, in attract mode (no input), with
nine games and different filter/image settings so the video path is trained too:
sfiii3 (CPS3, 400 s), sfa3 and ssf2t (CPS2), sf2ce and ffight (CPS1), kof98 and mslug
(Neo Geo), ddonpach (Cave) and outrun (Sega), 150 s each. The counters depend on the
code that runs, not on the speed, so they are valid for the real device.
The `.gcda` of objects that never ran (1042 of 1174) were deleted with `prune_gcda.py`,
so GCC compiles them with its normal heuristics instead of as cold code.

Recording it again:

    pgo/miyoomini/build.sh generate
    pgo/miyoomini/train_qemu.sh /path/to/roms
    rm -rf pgo/miyoomini/profile
    cp -a pgo/miyoomini/gcov/mnt/SDCARD/pgo_tmp/prof pgo/miyoomini/profile
    python3 pgo/miyoomini/prune_gcda.py pgo/miyoomini/profile
    pgo/miyoomini/build.sh use

## Measured

Miyoo Mini Plus, 1400 MHz, SimpleMenu paused, no audio (so no frame limit: the
emulator skips drawing and runs as fast as it can). Frames emulated in 70 s:

| Game | plain build | LTO | LTO + PGO |
|---|---|---|---|
| sfiii3 (CPS3) | 7337 | 7810 (+6 %) | 8657 (+18 %) |
| mslug (Neo Geo) | 12905 | 12994 (+1 %) | 15602 (+21 %) |
| kof98 (Neo Geo) | 11363 | 10549 (-7 %) | 12892 (+13 %) |
| sfa3 (CPS2) | 10738 | 10782 (+0 %) | 14444 (+35 %) |

All four games are in the training list; drivers that were not trained may gain less.
