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
                 goodbye.seq/.ans/.asc, about.seq/.ans/.asc,
                 files.seq/.ans/.asc. No motd ships: add
                 motd.asc/.ans/.seq to show one after login.
                 Hand-drawn art from PETSCII/ANSI editors can replace any of
                 these; the BBS only cares about the file name and extension.
                 HELP is generated from the command table, so no help screen ships.

                 Layout rules: PETSCII and ASCII lines stay under 40 columns
                 (a 40th character auto-wraps on a C64). ANSI art is 80 columns.
                 @BBS@ is the software and prints the name with a real µ on
                 ANSI, "u" elsewhere. @BOARD@ is what this board calls itself
                 (board_name in system.cfg), falling back to @BBS@ when unset.

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
    "F": ["###", "#..", "##.", "#..", "#.."],
    "I": ["###", ".#.", ".#.", ".#.", "###"],
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




# --------------------------------------------------------------------------
# The wordmark: a 6x12 pixel face drawn with CP437 half-block characters.
#
# A half block fills the top or the bottom of a cell, so a text row carries
# two pixel rows and a letter can have a 2px stroke, a shoulder and a curve.
# The old 3x5 face below is still used for PETSCII, where there is no
# equivalent trick and 40 columns to fit into.
#
# Capitals stand 10 pixels tall on a baseline two rows from the bottom. The
# micro sign is lowercase, so it is set at an x-height and its stem carries
# on below: a real descender, not a capital squashed to make room.
#
# Term::cp437 re-encodes these for a UTF-8 terminal, so one file serves both.
# --------------------------------------------------------------------------
BLK_FULL, BLK_TOP, BLK_BOT = 0xDB, 0xDF, 0xDC
WBLANK = "......"

WCAPS = {
    "U": ["##..##", "##..##", "##..##", "##..##", "##..##", "##..##", "##..##", "##..##", "######", ".####."],
    "N": ["##..##", "##..##", "###.##", "###.##", "###.##", "######", "##.###", "##.###", "##.###", "##..##"],
    "L": ["##....", "##....", "##....", "##....", "##....", "##....", "##....", "##....", "######", "######"],
    "E": ["######", "######", "##....", "##....", "#####.", "#####.", "##....", "##....", "######", "######"],
    "A": [".####.", "##..##", "##..##", "##..##", "######", "######", "##..##", "##..##", "##..##", "##..##"],
    "S": [".#####", "##...#", "##....", "##....", ".####.", "..####", "....##", "##..##", "#####.", ".####."],
    "H": ["##..##", "##..##", "##..##", "##..##", "######", "######", "##..##", "##..##", "##..##", "##..##"],
    "D": ["#####.", "##..##", "##..##", "##..##", "##..##", "##..##", "##..##", "##..##", "##..##", "#####."],
    "F": ["######", "######", "##....", "##....", "#####.", "#####.", "##....", "##....", "##....", "##...."],
    "I": ["######", "######", "..##..", "..##..", "..##..", "..##..", "..##..", "..##..", "######", "######"],
}
WFONT = {k: v + [WBLANK, WBLANK] for k, v in WCAPS.items()}
WFONT["\u00b5"] = [WBLANK, WBLANK,
                    "##..##", "##..##", "##..##", "##..##", "##..##", "##..##", "##..##", "######",
                    "##....", "##...."]

WORD = "\u00b5NLEASHED"


def wordmark_pixels(word=WORD, gap=1):
    rows = [""] * 12
    for i, ch in enumerate(word):
        for r in range(12):
            rows[r] += WFONT[ch][r] + ("." * gap if i < len(word) - 1 else "")
    return rows


def fold_half_blocks(px):
    """Pixel rows folded into half-block cells: two pixel rows become one
    text row. None is a cell with no ink in either half."""
    out = []
    for r in range(0, len(px), 2):
        top, bot = px[r], px[r + 1]
        row = []
        for c in range(len(top)):
            hi, lo = top[c] == "#", bot[c] == "#"
            row.append(BLK_FULL if hi and lo else BLK_TOP if hi else BLK_BOT if lo else None)
        out.append(row)
    return out


def wordmark_cells(word=WORD):
    """Pixel rows folded into half-block cells: 12 pixels become 6 rows."""
    return fold_half_blocks(wordmark_pixels(word))


def caps_cells(word, gap=1):
    """A word in the 6x10 caps only: 10 pixels become 5 text rows. No blank
    rows underneath, because nothing here has a descender and a door screen
    cannot spare the height."""
    px = [""] * 10
    for i, ch in enumerate(word):
        for r in range(10):
            px[r] += WCAPS[ch][r] + ("." * gap if i < len(word) - 1 else "")
    return fold_half_blocks(px)


def ansi_wordmark(indent, gradient):
    """The wordmark in CP437 half blocks, one colour per row."""
    out = bytearray()
    for r, row in enumerate(wordmark_cells()):
        line = bytearray(b" " * indent)
        line += sgr(gradient[min(r, len(gradient) - 1)])
        trail = 0
        for cell in row:
            if cell is None:
                trail += 1
            else:
                line += b" " * trail
                trail = 0
                line.append(cell)
        out += bytes(line) + b"\r\n@DELAY:90@"
    return bytes(out)



def centered(text, width=80):
    return b" " * ((width - len(text)) // 2) + text.encode()


# ==========================================================================
# welcome
# ==========================================================================
def make_welcome_ans():
    b = bytearray()
    b += f"{ESC}[0m{ESC}[2J{ESC}[H\r\n".encode()
    logo_w = len(wordmark_cells()[0])
    b += ansi_wordmark((80 - logo_w) // 2,
                       ["1;37", "1;36", "0;36", "1;34", "0;34", "0;34"])
    b += b"\r\n"
    b += sgr("1;37") + centered(MOTTO) + b"\r\n"
    tag = "no web  \xf9  no cloud  \xf9  no browser  \xf9  real hardware"
    b += sgr("0;36") + b" " * ((80 - len(tag)) // 2) + tag.encode("latin-1") + b"\r\n\r\n"
    b += sgr("0;34") + b"  " + bytes([H_DOUBLE]) * 76 + b"\r\n"
    b += (b"    " + sgr("1;36") + bytes([BULLET]) + sgr("0;32") + b" Node " + sgr("1;33") + b"@NODE@"
          + sgr("0;32") + b" of " + sgr("1;33") + b"@NODES@"
          + b"      " + sgr("1;36") + bytes([BULLET]) + sgr("0;32") + b" Terminal " + sgr("1;33") + b"@TERM@"
          + b"      " + sgr("1;36") + bytes([BULLET]) + sgr("0;32") + b" " + sgr("1;33") + b"@DATE@ @TIME@\r\n")
    b += (b"    " + sgr("1;36") + bytes([BULLET]) + b" " + sgr("1;33") + b"@BOARD@"
          + sgr("0;36") + b"  running " + sgr("0;37") + b"@BBS@ v@VER@"
          + sgr("0;36") + b" on a microcontroller\r\n")
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
    s += pet("yellow", " @BOARD@\n")
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
        " @BOARD@",
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
    s += pet("white", " @BOARD@\n")
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
@BOARD@ node @NODE@ is free again.
@DATE@ @TIME@@DELAY:400@
(C) 2026 Robert Mech  GPLv2+
"""


def make_goodbye_ans():
    b = bytearray()
    b += sgr("0;34") + bytes([H_DOUBLE]) * 60 + b"\r\n"
    b += sgr("1;36") + b"Stay unleashed, " + sgr("1;33") + b"@USER@" + sgr("1;36") + b".\r\n"
    b += sgr("1;37") + b"@BOARD@" + sgr("0;36") + b" node @NODE@ is free again.\r\n"
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


# ==========================================================================
# about (the ABOUT command; plain text so a sysop can rewrite it freely)
# ==========================================================================
ABOUT_ASC = """--------------------------------------
 @BBS@ v@VER@
--------------------------------------
 A telnet BBS on a bare ESP32.
 Node @NODE@ of @NODES@, you are @USER@.

 (C) 2026 Robert Mech
 Free software: GNU GPL v2 or later.
 Source and license: see the repo.

 Built on ESP-IDF, FreeRTOS, lwIP and
 littlefs. Their notices ship with it.
--------------------------------------
"""


def make_about_seq():
    s = bytearray()
    s += pet_rule("cyan")
    s += pet("white", " @BBS@", "grey", " v@VER@\n")
    s += pet_rule("cyan")
    s += pet("lgreen", " A telnet BBS on a bare ESP32.\n")
    s += pet("grey", " Node @NODE@ of @NODES@, you are @USER@.\n")
    s += pet("white", " (C) 2026 Robert Mech\n")
    s += pet("grey", " Free software: GNU GPL v2 or later.\n")
    s += pet("grey", " Source and license: see the repo.\n")
    s += pet("cyan", " ESP-IDF, FreeRTOS, lwIP, littlefs.\n")
    s += pet_rule("cyan")
    return bytes(s)


def make_about_ans():
    b = bytearray()
    b += sgr("0;34") + bytes([H_DOUBLE]) * 60 + b"\r\n"
    b += sgr("1;37") + b"@BBS@" + sgr("0;37") + b" v@VER@" + sgr("0;36") + b"  a telnet BBS on a bare ESP32\r\n"
    b += sgr("0;34") + bytes([H_DOUBLE]) * 60 + b"\r\n"
    b += sgr("0;37") + b" Node @NODE@ of @NODES@, you are " + sgr("1;33") + b"@USER@" + sgr("0;37") + b"\r\n\r\n"
    b += sgr("1;37") + b" (C) 2026 Robert Mech\r\n"
    b += sgr("0;37") + b" Free software: GNU General Public License v2 or later.\r\n"
    b += sgr("0;37") + b" Source and license: see the repository.\r\n\r\n"
    b += sgr("0;36") + b" Built on ESP-IDF, FreeRTOS, lwIP and littlefs; their notices\r\n"
    b += sgr("0;36") + b" travel with the firmware.\r\n"
    b += sgr("0;34") + bytes([H_DOUBLE]) * 60 + sgr("0") + b"\r\n"
    return bytes(b)


# ---------------------------------------------------------------------------
# privacy: the disclosure, four pages separated by form feeds. The player
# treats 0x0C as "stop here, wait for a key, clear the screen", so this reads
# like a short document rather than a wall of text sliding past.
#
# It is an ordinary screen file on purpose. A sysop should be able to say
# this in their own words for their own board, without a compiler.
#
# 39 columns so a C64 sees the same shape as everything else.
# ---------------------------------------------------------------------------
PRIVACY_PAGES = [
    [   ("h", "THE RISKS OF AN UNENCRYPTED BBS"),
        ("s", "and what it means for your privacy"),
        ("", ""),
        ("t", "Four short pages. They take a minute,"),
        ("t", "and they are the minute worth spending."),
        ("", ""),
        ("y", "TELNET IS NOT ENCRYPTED"),
        ("", ""),
        ("t", "Telnet has no encryption. It never has,"),
        ("t", "and on this board it never will."),
        ("", ""),
        ("t", "Everything you type crosses the network"),
        ("t", "as readable text. What you say, and the"),
        ("t", "password you type to get in."),
        ("", ""),
        ("t", "That is not an oversight. It is the"),
        ("t", "price of letting a 1982 computer call,"),
        ("t", "and a C64 cannot do encryption. Better"),
        ("t", "to tell you than quietly pretend."),
        ("", ""),
        ("d", "Page 1 of 4") ],

    [   ("y", "WHAT THAT ACTUALLY RISKS"),
        ("", ""),
        ("t", "Reading your password needs two things:"),
        ("t", "a sniffer, which is any program that"),
        ("t", "records network traffic, and a position"),
        ("t", "on the path between you and this board."),
        ("", ""),
        ("t", "Who has that? Whoever runs the wifi you"),
        ("t", "are on. Whoever runs the office"),
        ("t", "network. Your internet provider, and"),
        ("t", "the board's. Somebody who has put"),
        ("t", "themselves in the middle on purpose."),
        ("", ""),
        ("t", "Not a stranger on the internet, then."),
        ("t", "It takes access, and most people simply"),
        ("t", "do not have it."),
        ("", ""),
        ("t", "Low risk. Not no risk. Worth one unique"),
        ("t", "password, not an afternoon of worry."),
        ("", ""),
        ("d", "Page 2 of 4") ],

    [   ("y", "YOUR PASSWORD ON THIS BOARD"),
        ("", ""),
        ("t", "It is never stored as you typed it. It"),
        ("t", "is salted and hashed with SHA-256, a"),
        ("t", "thousand rounds, and only the result is"),
        ("t", "written down. Nobody can read it back,"),
        ("t", "including the sysop."),
        ("", ""),
        ("t", "That protects the file if the file is"),
        ("t", "stolen. It does nothing for the wire,"),
        ("t", "where you typed it in the clear."),
        ("", ""),
        ("t", "And a hash is not magic. A common"),
        ("t", "password still falls to a lookup table."),
        ("", ""),
        ("d", "Page 3 of 4") ],

    [   ("y", "WHAT THIS BOARD KNOWS"),
        ("", ""),
        ("t", "The sysop sees your handle, the address"),
        ("t", "you called from, when you called and for"),
        ("t", "how long, and the last command you ran."),
        ("t", "Staff can watch a node. Messages you"),
        ("t", "leave sit in a file until they are read."),
        ("", ""),
        ("t", "Assume whoever owns the machine can read"),
        ("t", "what is on it. That is true everywhere."),
        ("t", "Here you at least know who they are."),
        ("", ""),
        ("t", "Honestly? It is a hobby board on a five"),
        ("t", "dollar chip, and it is conversation."),
        ("", ""),
        ("g", "Use a password you use nowhere else."),
        ("", ""),
        ("d", "Page 4 of 4") ],
]

PRIV_ANSI = {"h": "1;36", "s": "0;36", "y": "1;33", "t": "0;37",
             "g": "1;32", "d": "1;30", "": "0;37"}
PRIV_PET  = {"h": "cyan", "s": "cyan", "y": "yellow", "t": "grey",
             "g": "lgreen", "d": "dgrey", "": "grey"}

FF = b"\x0c"          # the player's page break


def make_privacy_ans():
    out = bytearray()
    for n, page in enumerate(PRIVACY_PAGES):
        if n:
            out += FF
        for kind, line in page:
            out += sgr(PRIV_ANSI[kind]) + b" " + line.encode("ascii") + b"\r\n"
    out += sgr("0")
    return bytes(out)


def make_privacy_seq():
    out = bytearray()
    for n, page in enumerate(PRIVACY_PAGES):
        if n:
            out += FF
        else:
            out += pet("lower")
        for kind, line in page:
            out += pet(PRIV_PET[kind]) + pet_text(line) + pet("cr")
    return bytes(out)


def make_privacy_asc():
    out = bytearray()
    for n, page in enumerate(PRIVACY_PAGES):
        if n:
            out += FF
        for _, line in page:
            out += line.encode("ascii") + b"\r\n"
    return bytes(out)


# ==========================================================================
# rules: shown when somebody presses R to register, before they type a
# password. Two pages, because a 24 row terminal is the small one.
# ==========================================================================
RULES_P1 = [
    ("t", "THE HOUSE RULES"),
    ("r", ""),
    ("d", "You are about to make an account. This is the whole deal, and"),
    ("d", "it is shorter than the thing you clicked through this morning."),
    ("", ""),
    ("n", "1.  NO HATE."),
    ("i", "Argue with anybody about anything. Come after a person"),
    ("i", "for who they are and you are off the board. No warning,"),
    ("i", "no appeal, no long conversation about it."),
    ("", ""),
    ("n", "2.  NOTHING HERE IS ENCRYPTED."),
    ("i", "This is telnet, the way it was in 1969. Every word you"),
    ("i", "type crosses the network in the clear, your password"),
    ("i", "included. Anyone sharing a wire or an access point with"),
    ("i", "you can read the lot."),
    ("", ""),
    ("n", "3.  USE A PASSWORD YOU USE NOWHERE ELSE."),
    ("i", "This is the one that matters. If what you type here is"),
    ("i", "also the password on your mail, you have just handed"),
    ("i", "your mail to everyone between you and this board. Make"),
    ("i", "one up. It does not have to be clever. It has to be new."),
]

RULES_P2 = [
    ("t", "THE HOUSE RULES"),
    ("r", ""),
    ("n", "4.  THE SYSOP SEES EVERYTHING."),
    ("i", "Calls are logged. Chat is not private and neither is"),
    ("i", "mail on this board. Nothing here is a secret keeper."),
    ("i", "Do not type anything you would not say out loud in the"),
    ("i", "room."),
    ("", ""),
    ("n", "5.  IT IS A FIVE DOLLAR CHIP."),
    ("i", "The whole board is a microcontroller with less memory"),
    ("i", "than a floppy disk, sitting on a shelf somewhere. Be"),
    ("i", "patient with it. If it drops you, call back."),
    ("", ""),
    ("n", "6.  CHAOTIC NEUTRAL."),
    ("i", "Past all that, do as you like. Get along."),
    ("", ""),
    ("r", ""),
    ("d", "Still here? Good. Pick a handle and a password nobody else"),
    ("d", "has ever seen, and welcome aboard."),
]

# ==========================================================================
# newuser: the short version, once they are actually in
# ==========================================================================
NEWUSER = [
    ("t", "YOU ARE ON THE BOARD"),
    ("r", ""),
    ("d", "Welcome aboard, @USER@. Node @NODE@ of @NODES@ is yours."),
    ("", ""),
    ("n", "The short version, now that you have joined:"),
    ("", ""),
    ("b", "No hate. That is the one that gets you removed."),
    ("b", "Nothing here is encrypted. Never reuse a password."),
    ("b", "Chat and mail are not private. The sysop reads the logs."),
    ("b", "Be patient. It is a microcontroller, not a data centre."),
    ("", ""),
    ("r", ""),
    ("d", "?         the command list"),
    ("d", "PRIVACY   the long version of rule two, any time you like"),
    ("d", "CHAT      find out whether anyone else is awake"),
]

# ==========================================================================
# chat: the transition into the room
# ==========================================================================
CHATIN = [
    ("t", "ENTERING CHAT"),
    ("r", ""),
    ("d", "Everyone in the room sees what you type. /p sends a line to"),
    ("d", "one person, which is quieter but not private: it crosses the"),
    ("d", "wire in the clear and the sysop has the log. Nothing here is"),
    ("d", "kept once the buffer rolls over."),
    ("", ""),
    ("b", "/s        who else is here"),
    ("b", "/p n      a line to one person"),
    ("b", "/welcome  read this again"),
    ("b", "/help     the rest of the commands"),
    ("b", "/q        leave, or press ESC"),
    ("", ""),
    ("r", ""),
]


def ansi_page(lines, width=76):
    """One screen of headed text. 't' title, 'r' rule, 'n' note, 'b' bullet,
    'd' body, '' blank."""
    b = bytearray()
    for kind, text in lines:
        if kind == "t":
            pad = (width - len(text)) // 2
            b += sgr("1;37") + b" " * (2 + pad) + text.encode("ascii") + b"\r\n"
        elif kind == "r":
            b += sgr("0;34") + b"  " + bytes([H_LINE]) * width + b"\r\n"
        elif kind == "n":
            b += sgr("1;36") + b"  " + text.encode("ascii") + b"\r\n"
        elif kind == "b":
            b += sgr("0;32") + b"      " + bytes([BULLET]) + b" " + sgr("0;37") + text.encode("ascii") + b"\r\n"
        elif kind == "i":
            b += sgr("0;37") + b"        " + text.encode("ascii") + b"\r\n"
        elif kind == "d":
            b += sgr("0;37") + b"  " + text.encode("ascii") + b"\r\n"
        else:
            b += b"\r\n"
    return bytes(b)


def ascii_page(lines, width=76):
    out = []
    for kind, text in lines:
        if kind == "t":
            out.append(" " * (2 + (width - len(text)) // 2) + text)
        elif kind == "r":
            out.append("  " + "-" * width)
        elif kind == "b":
            out.append("      * " + text)
        elif kind == "i":
            out.append("        " + text)
        elif kind in ("n", "d"):
            out.append("  " + text)
        else:
            out.append("")
    return ("\n".join(out) + "\n").encode("ascii")


def make_rules_ans():
    return (b"@CLS@" + ansi_page(RULES_P1) + FF +
            b"@CLS@" + ansi_page(RULES_P2) + sgr("0"))


def make_rules_asc():
    return b"@CLS@" + ascii_page(RULES_P1) + FF + b"@CLS@" + ascii_page(RULES_P2)


def make_newuser_ans():
    return b"@CLS@" + ansi_page(NEWUSER) + sgr("0")


def make_newuser_asc():
    return b"@CLS@" + ascii_page(NEWUSER)


def make_chatin_ans():
    return b"@CLS@" + ansi_page(CHATIN) + sgr("0")


def make_chatin_asc():
    return b"@CLS@" + ascii_page(CHATIN)


# ==========================================================================
# files: the door into the file subsystem
#
# The files plugin clears the screen, plays this, prints one blank line and
# then draws the numbered area menu underneath. So this is a lintel and not
# a mural: seven rows on 40 columns and eight on 80, which leaves a 24-row
# terminal room for a title bar, ten areas and the prompt without the top of
# the door scrolling away.
#
# PETSCII note. Term::begin locks the mixed-case charset (0x0E then 0x08),
# so the graphics that survive are 0xA0-0xBF, plus 0xC0 and 0xDB-0xDF:
# 0xC1-0xDA are the capitals in that charset, which takes the rounded box
# corners off the table. Everything used here is from the range that stays:
#   0xA4  lower one eighth block    1px, the thin end of the top rule
#   0xAF  lower one quarter block   2px, a rule hugging the row below it
#   0xA3  upper one eighth block    1px, the thin end of the bottom rule
#   0xB7  upper one quarter block   2px, a rule hugging the row above it
#   0xA7  right one eighth block    1px vertical hairline
# Those quarter-block rules are the PETSCII way to draw a line that touches
# what it frames. 0xC0 (the full-cell horizontal) sits in the middle of its
# row and leaves a gap, which is right for a section rule and wrong for a
# lintel. And because the eighth and the quarter are different glyphs and
# not different colours, a rule tapers at its ends without a second colour
# being introduced, which CP437 has no way to do at all.
# ==========================================================================
FILES_SUB80 = "the file library"
FILES_SUB40 = "file library"

PET_LOW_8 = 0xA4          # lower one eighth block   (1 pixel)
PET_LOW_4 = 0xAF          # lower one quarter block  (2 pixels)
PET_TOP_8 = 0xA3          # upper one eighth block
PET_TOP_4 = 0xB7          # upper one quarter block
PET_HAIR_R = 0xA7         # right one eighth block


def pet_taper(thin, thick, width, ends=4):
    """A rule that thins at both ends. PETSCII has three weights below a
    half block, so a fade is drawn by changing glyph rather than colour,
    which is the one thing 40 columns can do that CP437 cannot."""
    mid = width - 2 * ends
    return bytes([thin]) * ends + bytes([thick]) * mid + bytes([thin]) * ends


def make_files_ans():
    """80 columns, 8 rows. The mark in half blocks with the small text set
    against it on the right, because a 34 column mark on an 80 column screen
    leaves half the width black otherwise."""
    cells = caps_cells("FILES")           # 5 rows of 34 cells
    w = len(cells[0])
    left = 4                              # the mark starts in column 5
    rule = 72                             # columns 5 to 76
    aside_col = 43                        # the small text starts in column 44
    # White on the cap line, cyan through the body, and the brightness put
    # back into the closing bar. A brighter row under a dimmer one breaks the
    # fade, which is what a light blue foot above a bright bar was doing.
    ramp = ["1;37", "1;36", "1;36", "0;36", "0;36"]
    # The small text is anchored rather than floated: the title sits on the
    # cap line of the mark and the stamp on its baseline, so the block reads
    # as set against the word instead of dropped next to it.
    aside = {
        0: ("1;37", FILES_SUB80.encode("ascii")),
        1: ("0;34", bytes([H_LINE]) * len(FILES_SUB80)),
        4: ("1;30", b"@DATE@  @TIME@"),
    }
    b = bytearray()
    b += sgr("0") + b"\r\n"
    # A lower half block puts its ink at the foot of the row, so the bar
    # lands hard against the top of the mark instead of floating above it.
    b += sgr("0;34") + b" " * left + bytes([BLK_BOT]) * rule + b"\r\n"
    for r, row in enumerate(cells):
        line = bytearray(b" " * left)
        line += sgr(ramp[r])
        for cell in row:
            line.append(cell if cell else 0x20)
        if r in aside:
            color, text = aside[r]
            line += b" " * (aside_col - (left + w))
            line += sgr(color) + text
        b += bytes(line) + b"\r\n"
    # The closing bar is brighter under the mark and dim past it: the eye is
    # told where the word ends without a second colour being introduced.
    b += sgr("1;36") + b" " * left + bytes([BLK_TOP]) * w
    b += sgr("0;34") + bytes([BLK_TOP]) * (rule - w) + b"\r\n"
    b += sgr("0")
    return bytes(b)


def make_files_seq():
    """40 columns, 7 rows. Redrawn rather than scaled: the 3x5 reverse-space
    face is the one that keeps a letter taller than it is wide on an 8x8
    cell, and the rules are quarter blocks rather than 0xC0."""
    rows = logo_rows("FILES")             # 5 rows of 19 cells
    indent = 1                            # the mark runs columns 2 to 20
    bar = 37                              # the rules run columns 2 to 38, so
    #                                       they start where the mark starts
    #                                       and end where the date stamp ends
    ramp = ["white", "cyan", "cyan", "cyan", "lblue"]
    aside = {1: pet_text(FILES_SUB40), 3: pet_text("@DATE@")}
    s = bytearray()
    s += pet("cyan") + b" " + pet_taper(PET_LOW_8, PET_LOW_4, bar) + pet("cr")
    for r, row in enumerate(rows):
        line = bytearray([PET[ramp[r]]])
        line += b" " * indent
        rvs = False
        for cell in row:
            want = cell == "#"
            if want != rvs:
                line.append(PET["rvs"] if want else PET["off"])
                rvs = want
            line += b" "
        if rvs:
            line.append(PET["off"])       # never leave reverse on at a CR
        line += b" "                                    # column 21
        line += bytes([PET["lblue"], PET_HAIR_R])       # column 22
        if r in aside:
            line += b" " + bytes([PET["grey"]]) + aside[r]   # column 24 on
        line.append(PET["cr"])
        s += bytes(line)
    s += pet("cyan") + b" " + pet_taper(PET_TOP_8, PET_TOP_4, bar) + pet("cr")
    s += bytes([PET["off"], PET["white"]])
    return bytes(s)


def make_files_asc():
    """39 columns, 7 rows, 7-bit. No colour and no reverse video, so the
    shape has to come from the rules and the gutter."""
    rows = logo_rows("FILES")
    out = [" " + "-" * 37]                # columns 2 to 38, as in the PETSCII
    aside = {1: FILES_SUB40, 3: "@DATE@"}
    for r, row in enumerate(rows):
        # " " + 19 cells fills columns 1 to 20, the divider sits in column 22
        # and the small text starts in column 24, the same gutter the PETSCII
        # and ANSI versions use.
        line = " " + "".join("#" if c == "#" else " " for c in row) + " |"
        if r in aside:
            line += " " + aside[r]
        out.append(line.rstrip())
    out.append(" " + "-" * 37)
    return ("\n".join(out) + "\n").encode("ascii")


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
        "about.seq": make_about_seq(),
        "about.ans": make_about_ans(),
        "about.asc": ABOUT_ASC.encode("ascii"),
        "rules.ans": make_rules_ans(),
        "rules.asc": make_rules_asc(),
        "newuser.ans": make_newuser_ans(),
        "newuser.asc": make_newuser_asc(),
        "chatin.ans": make_chatin_ans(),
        "chatin.asc": make_chatin_asc(),
        "files.ans": make_files_ans(),
        "files.seq": make_files_seq(),
        "files.asc": make_files_asc(),
        "privacy.seq": make_privacy_seq(),
        "privacy.ans": make_privacy_ans(),
        "privacy.asc": make_privacy_asc(),
    }
    for name, data in files.items():
        (OUT / name).write_bytes(data)
        print(f"{name:14s} {len(data):5d} bytes")


if __name__ == "__main__":
    main()
