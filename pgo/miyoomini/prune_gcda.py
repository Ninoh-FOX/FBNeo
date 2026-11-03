#!/usr/bin/env python3
"""prune_gcda.py DIR [--dry]: delete .gcda files whose counters are all zero (objects never run
during training). Without a .gcda GCC uses its static estimate for that file (normal -O2/-O3),
instead of treating every function as never executed (optimized for size)."""
import os, struct, sys
d = sys.argv[1]; dry = '--dry' in sys.argv
kept = gone = 0
for root, _, files in os.walk(d):
    for n in files:
        if not n.endswith('.gcda'): continue
        p = os.path.join(root, n); b = open(p, 'rb').read()
        if struct.unpack_from('<I', b, 0)[0] != 0x67636461: print('magic?', p); continue
        o = 12; live = False
        while o + 8 <= len(b):
            tag, ln = struct.unpack_from('<II', b, o); o += 8
            if (tag & 0xff000000) == 0x01000000 and tag != 0x01000000:  # counter records
                if any(b[o:o + 4 * ln]): live = True; break
            o += 4 * ln
        if live: kept += 1
        else:
            gone += 1
            if not dry: os.remove(p)
print('gcda con datos: %d, sin ejecutar (borrados%s): %d' % (kept, ' NO, simulacro' if dry else '', gone))
