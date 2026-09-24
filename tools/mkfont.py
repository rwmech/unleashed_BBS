#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""
===========================================================================
 µnleashed BBS
 Electronic freedom on a microcontroller.
===========================================================================

File:         tools/mkfont.py
Module:       Tools / panel font generator

Purpose:      Turns Spleen's BDF fonts into src/plugins/panel_font.h, the
              bitmap faces the panel plugin draws with (BBS_HAS_LCD boards
              only). Two sizes: 8x16 for every line of figures and 16x32 for
              the one number that matters. Printable ASCII, plus the micro
              sign, because the board's own name starts with one.

              Spleen is Frederic Cambus's, under the BSD 2-Clause licence,
              which is compatible with this project's GPL-2.0-or-later. The
              licence travels in the generated header, in tools/fonts/, in
              THIRD_PARTY_NOTICES.md and in a release's notices, as its
              second clause asks.

              The BDF files are not kept in the repository; the header is.
              To regenerate, fetch the release named below from
              https://github.com/fcambus/spleen and run:

                python3 tools/mkfont.py spleen-8x16.bdf spleen-16x32.bdf

Usage:        python3 tools/mkfont.py <8x16.bdf> <16x32.bdf> [out.h]
Libraries:    Python 3 standard library
Targets:      developer PC
See also:     src/plugins/panel_gfx.h, tools/fonts/SPLEEN-LICENSE

Copyright 2026 - Robert Mech
License:      GNU General Public License v2 or later
SPDX-License-Identifier: GPL-2.0-or-later
===========================================================================
"""

import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SPLEEN_VERSION = "2.2.0"
# Printable ASCII, then the extras in the order the renderer looks them up:
# index 95 is U+00B5, the micro sign.
CODES = list(range(0x20, 0x7F)) + [0xB5]


def read_bdf(path):
    """{codepoint: [row bitmaps as ints]} and the font's width and height."""
    glyphs, w, h = {}, 0, 0
    lines = Path(path).read_text(encoding="ascii", errors="replace").splitlines()
    i = 0
    version = ""
    while i < len(lines):
        t = lines[i].strip()
        if t.startswith("FONTBOUNDINGBOX"):
            f = t.split()
            w, h = int(f[1]), int(f[2])
        elif t.startswith("FONT_VERSION"):
            version = t.split(None, 1)[1].strip('"')
        elif t.startswith("STARTCHAR"):
            code, bbx, rows = None, None, []
            i += 1
            while not lines[i].startswith("ENDCHAR"):
                s = lines[i].strip()
                if s.startswith("ENCODING"):
                    code = int(s.split()[1])
                elif s.startswith("BBX"):
                    bbx = [int(x) for x in s.split()[1:]]
                elif s == "BITMAP":
                    i += 1
                    while not lines[i].startswith("ENDCHAR"):
                        rows.append(int(lines[i].strip(), 16))
                        i += 1
                    break
                i += 1
            if code is not None:
                glyphs[code] = (bbx, rows)
        i += 1
    if version != SPLEEN_VERSION:
        sys.exit(f"mkfont: {path} is Spleen {version or '?'}, expected {SPLEEN_VERSION}")
    return glyphs, w, h


def cell(glyph, w, h):
    """The glyph as a full w x h cell, rows MSB first, bytes per row w/8.
    Spleen's cells are all full-size with no offset, which is checked."""
    bbx, rows = glyph
    if bbx != [w, h, 0, bbx[3]] or len(rows) != h:
        sys.exit(f"mkfont: a glyph is not a full {w}x{h} cell: {bbx}")
    return rows


def emit(name, glyphs, w, h):
    bpr = (w + 7) // 8
    out = [f"// {w}x{h}: {len(CODES)} glyphs, {bpr} byte(s) a row, {h} rows, MSB is the left pixel",
           f"constexpr uint8_t {name}[{len(CODES)}][{bpr * h}] = {{"]
    for c in CODES:
        if c not in glyphs:
            sys.exit(f"mkfont: U+{c:04X} is missing from the {w}x{h} face")
        rows = cell(glyphs[c], w, h)
        data = []
        for r in rows:
            data += [(r >> (8 * (bpr - 1 - k))) & 0xFF for k in range(bpr)]
        label = chr(c) if 0x20 < c < 0x7F and chr(c) not in "\\" else f"U+{c:04X}"
        out.append("    { " + ", ".join(f"0x{b:02X}" for b in data) + f" }},  // {label}")
    out.append("};")
    return out


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__.split("Usage:")[1].split("Libraries:")[0].strip())
    small, sw, sh = read_bdf(sys.argv[1])
    big, bw, bh = read_bdf(sys.argv[2])
    if (sw, sh) != (8, 16) or (bw, bh) != (16, 32):
        sys.exit("mkfont: expected the 8x16 face then the 16x32 face")
    out_path = Path(sys.argv[3]) if len(sys.argv) > 3 else ROOT / "src/plugins/panel_font.h"
    licence = (ROOT / "tools/fonts/SPLEEN-LICENSE").read_text(encoding="utf-8").rstrip()
    head = [
        "/*",
        " * ===========================================================================",
        " *  µnleashed BBS",
        " *  Electronic freedom on a microcontroller.",
        " * ===========================================================================",
        " *",
        " * File:         src/plugins/panel_font.h",
        " * Module:       Plugins / panel (BBS_HAS_LCD boards only)",
        " *",
        " * GENERATED by tools/mkfont.py from Spleen " + SPLEEN_VERSION + " (spleen-8x16.bdf and",
        " * spleen-16x32.bdf). Do not edit by hand: run the tool.",
        " *",
        " * The glyphs are Spleen's, by Frederic Cambus, https://github.com/fcambus/spleen",
        " * under the BSD 2-Clause licence, reproduced here as it asks:",
        " *",
    ] + [(" * " + ln).rstrip() for ln in licence.splitlines()] + [
        " *",
        " * The arrangement into C arrays, and this file's place in the firmware, are",
        " * part of µnleashed BBS:",
        " *",
        " * Copyright 2026 - Robert Mech",
        " * License:      GNU General Public License v2 or later",
        " * SPDX-License-Identifier: GPL-2.0-or-later AND BSD-2-Clause",
        " * ===========================================================================",
        " */",
        "#pragma once",
        "#include <cstdint>",
        "",
        "namespace panelfont {",
        "",
        "// Glyph index: printable ASCII 0x20 to 0x7E at 0 to 94, then the micro",
        "// sign (U+00B5) at 95. Anything else is drawn as '?'.",
        f"constexpr uint8_t kGlyphs = {len(CODES)};",
        "constexpr uint8_t kMicro  = 95;",
        "",
    ]
    body = emit("kSmall", small, 8, 16) + [""] + emit("kBig", big, 16, 32)
    tail = ["", "} // namespace panelfont", ""]
    with open(out_path, "w", encoding="utf-8", newline="\n") as f:   # LF on every OS
        f.write("\n".join(head + body + tail))
    print(f"mkfont: wrote {out_path}")


if __name__ == "__main__":
    main()
