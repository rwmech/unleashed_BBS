#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""
===========================================================================
 µnleashed BBS
 Electronic freedom on a microcontroller.
===========================================================================

File:         tools/mkscreens.py
Module:       Tools / stock screen generator

Purpose:      Generates the stock µnleashed BBS display files in data/screens/:
                 welcome.seq/.ans/.asc, busy.seq/.ans/.asc,
                 goodbye.seq/.ans/.asc. No bulletin ships: add
                 bulletin.asc/.ans/.seq to show one after login.
                 Hand-drawn art from PETSCII/ANSI editors can replace any of
                 these; the BBS only cares about the file name and extension.
                 HELP is generated from the command table, so no help screen ships.

                 Layout rules: PETSCII and ASCII lines stay under 40 columns
                 (a 40th character auto-wraps on a C64). ANSI art is 80 columns.
                 @BBS@ prints the name with a real µ on ANSI and "u" elsewhere.

Usage:        python3 tools/mkscreens.py

Libraries:    Python 3 standard library only
Targets:      developer PC, Python 3
See also:     SCREENS.md

Copyright 2026 - Robert Mech
License:      GNU General Public License v2 or later
SPDX-License-Identifier: GPL-2.0-or-later

This program is free software; you can redistribute it and/or modify it
under the terms of the GNU General Public License as published by the
Free Software Foundation; either version 2 of the License, or (at your
option) any later version.

This program is distributed in the hope that it will be useful, but
WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program; if not, see <https://www.gnu.org/licenses/>. The full
text is in the LICENSE file at the top of this repository.
===========================================================================
"""

from pathlib import Path

OUT = Path(__file__).resolve().parent.parent / "data" / "screens"

TAGLINE = "No web. No cloud. No browser."
COPY40 = "(C) 2026 Robert Mech  GPLv2+"          # fits a 40-column C64 line
COPY80 = "(C) 2026 Robert Mech. Free software under the GPL, v2 or later."
MOTTO = "E L E C T R O N I C   F R E E D O M"

# --------------------------------------------------------------------------
# 3x5 block font for the logo: µ n L E A S H E D (the µ has a descender)
# --------------------------------------------------------------------------
FONT = {
    "µ": ["#.#", "#.#", "#.#", "###", "#.."],
    "n": ["...", "##.", "#.#", "#.#", "#.#"],
    "L": ["#..", "#..", "#..", "#..", "###"],
    "E": ["###", "#..", "##.", "#..", "###"],
    "A": [".#.", "#.#", "###", "#.#", "#.#"],
    "S": ["###", "#..", "###", "..#", "###"],
    "H": ["#.#", "#.#", "###", "#.#", "#.#"],
    "D": ["##.", "#.#", "#.#", "#.#", "##."],
}
LOGO = "µnLEASHED"


def logo_rows(word):
    """5 rows of '#'/'.' cells, one blank cell between letters."""
    rows = [""] * 5
    for i, ch in enumerate(word):
        for r in range(5):
            rows[r] += FONT[ch][r] + ("." if i < len(word) - 1 else "")
    return rows


# --------------------------------------------------------------------------
# PETSCII helpers (mixed-case charset)
# --------------------------------------------------------------------------
PET = {
    "clr": 0x93, "lower": 0x0E, "lock": 0x08, "rvs": 0x12, "off": 0x92,
    "black": 0x90, "white": 0x05, "red": 0x1C, "cyan": 0x9F, "purple": 0x9C,
    "green": 0x1E, "blue": 0x1F, "yellow": 0x9E, "orange": 0x81, "brown": 0x95,
    "lred": 0x96, "dgrey": 0x97, "grey": 0x98, "lgreen": 0x99, "lblue": 0x9A,
    "lgrey": 0x9B, "cr": 0x0D, "hline": 0xC0,
}


def pet_text(s):
    out = bytearray()
    for c in s:
        o = ord(c)
        if "a" <= c <= "z":
            out.append(o - 0x20)
        elif "A" <= c <= "Z":
            out.append(o + 0x80)
        elif c == "\n":
            out.append(0x0D)
        else:
            out.append(o)
    return bytes(out)


def pet(*parts):
    out = bytearray()
    for p in parts:
        if isinstance(p, int):
            out.append(p)
        elif p in PET:
            out.append(PET[p])
        else:
            out += pet_text(p)
    return bytes(out)


def pet_rule(color, width=38):
    return bytes([PET[color]]) + bytes([PET["hline"]]) * width + bytes([PET["cr"]])


def pet_logo_row(row, indent):
    """One logo row: reverse spaces for set cells, one character per cell."""
    out = bytearray(b" " * indent)
    rvs = False
    for cell in row:
        want = cell == "#"
        if want != rvs:
            out.append(PET["rvs"] if want else PET["off"])
            rvs = want
        out += b" "
    if rvs:
        out.append(PET["off"])
    out.append(PET["cr"])
    return bytes(out)


# --------------------------------------------------------------------------
# ANSI helpers (CP437)
# --------------------------------------------------------------------------
ESC = "\x1b"
BLOCK, SHADE_LIGHT, BULLET = 0xDB, 0xB0, 0xF9
H_LINE, H_DOUBLE = 0xC4, 0xCD


def sgr(code):
    return f"{ESC}[{code}m".encode("ascii")


def ansi_logo(indent, gradient, shadow="1;30"):
    """Double-width block logo with a drop shadow one cell down and right."""
    rows = logo_rows(LOGO)
    height, width = len(rows), len(rows[0])

    def on(r, c):
        return 0 <= r < height and 0 <= c < width and rows[r][c] == "#"

    out = bytearray()
    for r in range(height + 1):
        line = bytearray(b" " * indent)
        cur = None
        for c in range(width + 1):
            if on(r, c):
                want, cell = gradient[min(r, height - 1)], bytes([BLOCK, BLOCK])
            elif on(r - 1, c - 1):
                want, cell = shadow, bytes([SHADE_LIGHT, SHADE_LIGHT])
            else:
                want, cell = None, b"  "
            if want and want != cur:
                line += sgr(want)
                cur = want
            line += cell
        out += bytes(line.rstrip()) + b"\r\n@DELAY:90@"
    return bytes(out)


def centered(text, width=80):
    return b" " * ((width - len(text)) // 2) + text.encode()


# ==========================================================================
# welcome
# ==========================================================================
def make_welcome_ans():
    b = bytearray()
    b += f"{ESC}[0m{ESC}[2J{ESC}[H\r\n".encode()
    logo_w = (len(logo_rows(LOGO)[0]) + 1) * 2
    b += ansi_logo((80 - logo_w) // 2, ["1;37", "1;36", "0;36", "1;34", "0;34"])
    b += b"\r\n"
    b += sgr("1;37") + centered(MOTTO) + b"\r\n"
    tag = "no web  \xf9  no cloud  \xf9  no browser  \xf9  real hardware"
    b += sgr("0;36") + b" " * ((80 - len(tag)) // 2) + tag.encode("latin-1") + b"\r\n\r\n"
    b += sgr("0;34") + b"  " + bytes([H_DOUBLE]) * 76 + b"\r\n"
    b += (b"    " + sgr("1;36") + bytes([BULLET]) + sgr("0;32") + b" Node " + sgr("1;33") + b"@NODE@"
          + sgr("0;32") + b" of " + sgr("1;33") + b"@NODES@"
          + b"      " + sgr("1;36") + bytes([BULLET]) + sgr("0;32") + b" Terminal " + sgr("1;33") + b"@TERM@"
          + b"      " + sgr("1;36") + bytes([BULLET]) + sgr("0;32") + b" " + sgr("1;33") + b"@DATE@ @TIME@\r\n")
    b += (b"    " + sgr("1;36") + bytes([BULLET]) + b" " + sgr("1;37") + b"@BBS@" + sgr("0;37")
          + b" v@VER@  " + sgr("0;36") + b"a BBS that lives on a microcontroller\r\n")
    b += sgr("0;34") + b"  " + bytes([H_DOUBLE]) * 76 + b"\r\n"
    b += sgr("1;30") + b" " * ((80 - len(COPY80)) // 2) + COPY80.encode() + b"\r\n"
    b += sgr("0;37") + b"    Connecting you @SPIN:900@" + sgr("1;32") + b"unleashed" + sgr("0") + b"\r\n"
    return bytes(b)


def make_welcome_seq():
    rows = logo_rows(LOGO)
    indent = (40 - len(rows[0])) // 2
    ramp = ["white", "cyan", "lblue", "purple", "lred"]
    s = bytearray()
    s += pet("clr", "lower", "lock", "cr")
    for row, col in zip(rows, ramp):
        s += bytes([PET[col]]) + pet_logo_row(row, indent) + pet("@DELAY:90@")
    s += pet("cr")
    s += pet_rule("cyan")
    s += pet("white", " " + MOTTO + "\n")
    s += pet("grey", "    " + TAGLINE + "\n")
    s += pet_rule("cyan")
    s += pet("lgreen", " Node ", "yellow", "@NODE@", "lgreen", " of ", "yellow", "@NODES@",
             "lgreen", "   ", "yellow", "@TERM@\n")
    s += pet("lgreen", " ", "yellow", "@DATE@ @TIME@\n")
    s += pet("white", " @BBS@", "grey", " v@VER@\n")
    s += pet("grey", " " + COPY40 + "\n")
    s += pet_rule("cyan")
    s += pet("grey", " Connecting you @SPIN:900@", "lgreen", "unleashed\n")
    return bytes(s)


def make_welcome_asc():
    rows = logo_rows(LOGO)
    indent = (40 - len(rows[0])) // 2
    out = ["-" * 38, ""]
    for row in rows:
        out.append(" " * indent + "".join("#" if c == "#" else " " for c in row).rstrip() + "@DELAY:90@")
    out += [
        "",
        " " + MOTTO,
        "    " + TAGLINE,
        "-" * 38,
        " Node @NODE@ of @NODES@   Term @TERM@",
        " @DATE@ @TIME@",
        " @BBS@ v@VER@",
        " " + COPY40,
        "-" * 38,
        " Connecting you @SPIN:900@unleashed",
        "",
    ]
    return "\n".join(out).encode("ascii")


# ==========================================================================
# busy (tests look for "lines are busy")
# ==========================================================================
BUSY_ASC = """@CLS@--------------------------------------
 @BBS@
--------------------------------------
 Sorry, all @NODES@ lines are busy.
 Every node is unleashed right now.
 Please try your call again soon.
--------------------------------------
"""


def make_busy_ans():
    w = 50
    name_len = len("µnleashed BBS")          # display width of @BBS@
    left = b" " * ((80 - (w + 2)) // 2)

    def boxed(color, text, shown_len):
        return (sgr("0;31") + left + bytes([0xBA]) + sgr(color) + b"   " + text
                + b" " * (w - 3 - shown_len) + sgr("0;31") + bytes([0xBA]) + b"\r\n")

    b = bytearray()
    b += f"{ESC}[0m{ESC}[2J{ESC}[H\r\n\r\n".encode()
    b += sgr("0;31") + left + bytes([0xC9]) + bytes([0xCD]) * w + bytes([0xBB]) + b"\r\n"
    b += boxed("1;37", b"@BBS@", name_len)
    b += sgr("0;31") + left + bytes([0xC7]) + bytes([0xC4]) * w + bytes([0xB6]) + b"\r\n"
    msg1 = "Sorry, all @NODES@ lines are busy."
    b += boxed("1;33", msg1.encode(), len("Sorry, all 6 lines are busy."))
    msg2 = "Every node is unleashed right now."
    b += boxed("0;36", msg2.encode(), len(msg2))
    msg3 = "Please try your call again soon."
    b += boxed("0;37", msg3.encode(), len(msg3))
    b += sgr("0;31") + left + bytes([0xC8]) + bytes([0xCD]) * w + bytes([0xBC]) + sgr("0") + b"\r\n"
    return bytes(b)


def make_busy_seq():
    s = bytearray()
    s += pet("clr", "lower", "lock", "cr")
    s += pet_rule("red")
    s += pet("white", " @BBS@\n")
    s += pet_rule("red")
    s += pet("yellow", " Sorry, all @NODES@ lines are busy.\n")
    s += pet("cyan", " Every node is unleashed right now.\n")
    s += pet("grey", " Please try your call again soon.\n")
    s += pet_rule("red")
    return bytes(s)


# ==========================================================================
# goodbye
# ==========================================================================
GOODBYE_ASC = """--------------------------------------
Stay unleashed, @USER@.
@BBS@ node @NODE@ is free again.
@DATE@ @TIME@@DELAY:400@
(C) 2026 Robert Mech  GPLv2+
"""


def make_goodbye_ans():
    b = bytearray()
    b += sgr("0;34") + bytes([H_DOUBLE]) * 60 + b"\r\n"
    b += sgr("1;36") + b"Stay unleashed, " + sgr("1;33") + b"@USER@" + sgr("1;36") + b".\r\n"
    b += sgr("1;37") + b"@BBS@" + sgr("0;36") + b" node @NODE@ is free again.\r\n"
    b += sgr("0;37") + b"@DATE@ @TIME@@DELAY:400@\r\n"
    b += sgr("1;30") + COPY80.encode() + sgr("0") + b"\r\n"
    return bytes(b)


def make_goodbye_seq():
    s = bytearray()
    s += pet_rule("cyan")
    s += pet("cyan", "Stay unleashed, ", "yellow", "@USER@", "cyan", ".\n")
    s += pet("white", "@BBS@", "grey", " node @NODE@ is free again.\n")
    s += pet("grey", "@DATE@ @TIME@@DELAY:400@\n")
    s += pet("grey", COPY40 + "\n")
    return bytes(s)


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    files = {
        "welcome.seq": make_welcome_seq(),
        "welcome.ans": make_welcome_ans(),
        "welcome.asc": make_welcome_asc(),
        "busy.seq": make_busy_seq(),
        "busy.ans": make_busy_ans(),
        "busy.asc": BUSY_ASC.encode("ascii"),
        "goodbye.seq": make_goodbye_seq(),
        "goodbye.ans": make_goodbye_ans(),
        "goodbye.asc": GOODBYE_ASC.encode("ascii"),
    }
    for name, data in files.items():
        (OUT / name).write_bytes(data)
        print(f"{name:14s} {len(data):5d} bytes")


if __name__ == "__main__":
    main()
