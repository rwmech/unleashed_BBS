#!/usr/bin/env python3
"""
File:        tools/mkscreens.py
Description: Generates the stock display files in data/screens/:
             welcome.seq (PETSCII 40), welcome.ans (ANSI CP437),
             welcome.asc (ASCII), help.asc, goodbye.asc, goodbye.ans,
             busy.asc, busy.ans, busy.seq. No bulletin ships: add
             bulletin.asc/.ans/.seq to show one after login.
             Hand-drawn art from PETSCII/ANSI editors can replace any of
             these; the BBS only cares about the file name and extension.
Listing:     COMPLETE FILE
Libraries:   Python 3 standard library only
Usage:       python3 tools/mkscreens.py
"""
from pathlib import Path

OUT = Path(__file__).resolve().parent.parent / "data" / "screens"

# --------------------------------------------------------------------------
# 3x5 block font
# --------------------------------------------------------------------------
FONT = {
    "B": ["##.", "#.#", "##.", "#.#", "##."],
    "S": ["###", "#..", "###", "..#", "###"],
    "E": ["###", "#..", "##.", "#..", "###"],
    "P": ["##.", "#.#", "##.", "#..", "#.."],
    "3": ["###", "..#", ".##", "..#", "###"],
    "2": ["###", "..#", "###", "#..", "###"],
    " ": ["..", "..", "..", "..", ".."],
}


def logo_rows(word):
    """Return 5 rows of '#'/'.' cells with a 1-cell gap between letters."""
    rows = [""] * 5
    for i, ch in enumerate(word):
        glyph = FONT[ch]
        for r in range(5):
            rows[r] += glyph[r] + ("." if i < len(word) - 1 else "")
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


def pet_logo_row(row, indent):
    out = bytearray(b" " * indent)
    rvs = False
    for cell in row:
        want = cell == "#"
        if want != rvs:
            out.append(PET["rvs"] if want else PET["off"])
            rvs = want
        out += b"  "
    if rvs:
        out.append(PET["off"])
    out.append(PET["cr"])
    return bytes(out)


def make_welcome_seq():
    w = 38
    rows = logo_rows("BBS")
    width = len(rows[0]) * 2
    indent = (w - width) // 2
    colors = ["lblue", "cyan", "lgreen", "yellow", "orange"]
    s = bytearray()
    s += pet("clr", "lower", "lock")
    s += bytes([PET["cyan"]]) + bytes([PET["hline"]]) * w + bytes([PET["cr"]])
    s += bytes([PET["cr"]])
    for row, col in zip(rows, colors):
        s += bytes([PET[col]]) + pet_logo_row(row, indent) + pet("@DELAY:120@")
    s += bytes([PET["cr"]])
    s += pet("white", "   E S P 3 2    I O T    B B S\n")
    s += pet("grey", "  Terminal server for the maker crowd\n")
    s += bytes([PET["cyan"]]) + bytes([PET["hline"]]) * w + bytes([PET["cr"]])
    s += pet("lgreen", " Node ", "yellow", "@NODE@", "lgreen", " of ", "yellow", "@NODES@",
             "lgreen", "   Term ", "yellow", "@TERM@\n")
    s += pet("lgreen", " Version ", "yellow", "@VER@\n")
    s += bytes([PET["cyan"]]) + bytes([PET["hline"]]) * w + bytes([PET["cr"]])
    s += pet("grey", " Connecting you @SPIN:900@", "lgreen", "done\n")
    return bytes(s)


# --------------------------------------------------------------------------
# ANSI helpers (CP437)
# --------------------------------------------------------------------------
ESC = "\x1b"


def sgr(code):
    return f"{ESC}[{code}m".encode("ascii")


def make_welcome_ans():
    w = 72
    rows = logo_rows("ESP32 BBS")
    width = len(rows[0]) * 2
    indent = (80 - width) // 2
    box_in = (80 - (w + 2)) // 2
    grad = ["1;34", "1;36", "0;36", "1;32", "0;32"]
    b = bytearray()
    b += f"{ESC}[0m{ESC}[2J{ESC}[H".encode()
    b += sgr("0;34") + b" " * box_in + bytes([0xC9]) + bytes([0xCD]) * w + bytes([0xBB]) + b"\r\n\r\n"
    for row, col in zip(rows, grad):
        line = bytearray(b" " * indent)
        for cell in row:
            line += bytes([0xDB, 0xDB]) if cell == "#" else b"  "
        b += sgr(col) + bytes(line.rstrip()) + b"\r\n" + b"@DELAY:100@"
    b += b"\r\n"
    title = "E S P 3 2    I O T    T E R M I N A L    S E R V E R"
    b += sgr("1;37") + b" " * ((80 - len(title)) // 2) + title.encode() + b"\r\n"
    b += b"\r\n"
    b += sgr("0;34") + b" " * box_in + bytes([0xC7]) + bytes([0xC4]) * w + bytes([0xB6]) + b"\r\n"
    pad = b" " * (box_in + 3)
    b += pad + sgr("0;32") + b"Node " + sgr("1;33") + b"@NODE@" + sgr("0;32") + b" of " \
        + sgr("1;33") + b"@NODES@" + sgr("0;32") + b"      Terminal " + sgr("1;33") + b"@TERM@\r\n"
    b += pad + sgr("0;32") + b"Version " + sgr("1;33") + b"@VER@" + sgr("0;32") \
        + b"       Built for makers, " + bytes([0xF9]) + b" old-school style\r\n"
    b += sgr("0;34") + b" " * box_in + bytes([0xC8]) + bytes([0xCD]) * w + bytes([0xBC]) + b"\r\n"
    b += sgr("0;37") + pad + b"Connecting you @SPIN:900@" + sgr("1;32") + b"done" + sgr("0") + b"\r\n"
    return bytes(b)


def make_welcome_asc():
    rows = logo_rows("BBS")
    width = len(rows[0]) * 2
    indent = (38 - width) // 2
    out = ["-" * 38, ""]
    for row in rows:
        out.append(" " * indent + "".join("##" if c == "#" else "  " for c in row).rstrip() + "@DELAY:120@")
    out += [
        "",
        "   E S P 3 2    I O T    B B S",
        "  Terminal server for the maker crowd",
        "-" * 38,
        " Node @NODE@ of @NODES@   Term @TERM@",
        " Version @VER@",
        "-" * 38,
        " Connecting you @SPIN:900@done",
        "",
    ]
    return "\n".join(out).encode("ascii")


HELP = """Commands
--------------------------------------
[H]ELP    this screen, also ?
[W]HO     who is online
[M]EM     memory stats
[T]ERM    terminal type and size
[C]LS     clear screen
[F]X      old-school effects demo
TIME      clock and time left
LAST      recent callers
PAGE n m  send message m to node n
DND       pages off/on
BAUD n    emulate n bps, BAUD OFF
[G]       log off, asks first
BYE       log off now

Up-arrow recalls earlier commands.
Any key fast-forwards animations.
Space, Ctrl-C or RUN/STOP stops output.
"""

BUSY_ASC = """@CLS@@BBS@
--------------------------------------
Sorry, all @NODES@ lines are busy.
Please try your call later.
--------------------------------------
"""


def make_busy_ans():
    w = 44
    b = bytearray()
    b += f"{ESC}[0m{ESC}[2J{ESC}[H\r\n".encode()
    b += sgr("0;31") + b"  " + bytes([0xC9]) + bytes([0xCD]) * w + bytes([0xBB]) + b"\r\n"
    title = "@BBS@"
    b += sgr("0;31") + b"  " + bytes([0xBA]) + sgr("1;37") + b"  " + title.encode() \
        + b" " * (w - 2 - len("ESP32 IOT BBS")) + sgr("0;31") + bytes([0xBA]) + b"\r\n"
    b += sgr("0;31") + b"  " + bytes([0xC7]) + bytes([0xC4]) * w + bytes([0xB6]) + b"\r\n"
    msg1 = "Sorry, all @NODES@ lines are busy."
    msg2 = "Please try your call later."
    b += sgr("0;31") + b"  " + bytes([0xBA]) + sgr("1;33") + b"  " + msg1.encode() \
        + b" " * (w - 2 - len("Sorry, all 6 lines are busy.")) + sgr("0;31") + bytes([0xBA]) + b"\r\n"
    b += sgr("0;31") + b"  " + bytes([0xBA]) + sgr("0;37") + b"  " + msg2.encode() \
        + b" " * (w - 2 - len(msg2)) + sgr("0;31") + bytes([0xBA]) + b"\r\n"
    b += sgr("0;31") + b"  " + bytes([0xC8]) + bytes([0xCD]) * w + bytes([0xBC]) + sgr("0") + b"\r\n"
    return bytes(b)


def make_busy_seq():
    w = 38
    s = bytearray()
    s += pet("clr", "lower", "lock")
    s += bytes([PET["red"]]) + bytes([PET["hline"]]) * w + bytes([PET["cr"]])
    s += pet("white", " @BBS@\n")
    s += bytes([PET["red"]]) + bytes([PET["hline"]]) * w + bytes([PET["cr"]])
    s += pet("yellow", " Sorry, all @NODES@ lines are busy.\n")
    s += pet("grey", " Please try your call later.\n")
    s += bytes([PET["red"]]) + bytes([PET["hline"]]) * w + bytes([PET["cr"]])
    return bytes(s)

GOODBYE_ASC = """Thanks for calling, @USER@!
@BBS@ node @NODE@ is free again.
Call back soon.@DELAY:400@
"""


def make_goodbye_ans():
    b = bytearray()
    b += sgr("1;32") + b"Thanks for calling, " + sgr("1;33") + b"@USER@" + sgr("1;32") + b"!\r\n"
    b += sgr("0;36") + b"@BBS@ node @NODE@ is free again.\r\n"
    b += sgr("0;37") + b"Call back soon.@DELAY:400@" + sgr("0") + b"\r\n"
    return bytes(b)


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    files = {
        "welcome.seq": make_welcome_seq(),
        "welcome.ans": make_welcome_ans(),
        "welcome.asc": make_welcome_asc(),
        "help.asc": HELP.encode("ascii"),
        "goodbye.asc": GOODBYE_ASC.encode("ascii"),
        "goodbye.ans": make_goodbye_ans(),
        "busy.asc": BUSY_ASC.encode("ascii"),
        "busy.ans": make_busy_ans(),
        "busy.seq": make_busy_seq(),
    }
    for name, data in files.items():
        (OUT / name).write_bytes(data)
        print(f"{name:14s} {len(data):5d} bytes")


if __name__ == "__main__":
    main()
