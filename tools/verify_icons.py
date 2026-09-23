#!/usr/bin/env python3
"""Print generated icon bitmaps with column guides for verification."""
import sys
sys.path.insert(0, "tools")
import gen_assets as g

# keep module from regenerating
g.emit_icons = lambda *a, **k: None

ICON_ROWS = g.ICON_ROWS
for name, fn in g.ICONS.items():
    main_bits, detail_bits = fn()
    print(f"=== {name} (32x32) ===")
    print("    " + "".join(str(i // 10) if i % 10 == 0 else " " for i in range(32)))
    print("    " + "".join(str(i % 10) for i in range(32)))
    for yy in range(32):
        row = ""
        for xx in range(32):
            mi = main_bits[yy * ICON_ROWS + (xx >> 3)] & (1 << (xx & 7))
            de = detail_bits[yy * ICON_ROWS + (xx >> 3)] & (1 << (xx & 7))
            row += "#" if mi else ("o" if de else ".")
        print(f"{yy:2d} |{row}|")