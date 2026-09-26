#!/usr/bin/env python3
"""
===========================================================================
 µnleashed BBS
 Electronic freedom on a microcontroller.
===========================================================================

File:         tools/mkskin.py
Module:       Tools / panel skins

Purpose:      A panel skin's workbench, for a sysop at a PC. SKINS.md is the
              format; this is the same rules, checked before the card.

                python tools/mkskin.py check  skins/stock/c64
                    skin.txt read by the board's grammar, background.jpg by
                    its decoder's rules; every fault with its line.
                python tools/mkskin.py leds   placeholder.png --key drive=#FF00FF
                         --key activity=#00FFFF --key led=#FFFF00 --style 1541
                         -o skins/mine/skin.txt
                    the LEDs painted as solid key colours on a copy of the
                    art, turned into skin.txt lines (centre, diameter; strip
                    LEDs numbered left to right, then top to bottom). A key
                    text=#RRGGBB or clock=#RRGGBB gives those rectangles.
                python tools/mkskin.py preview skins/stock/c64 -o c64.png
                    the skin as the panel draws it: the LEDs lit, sample
                    status lines in the panel's own font.
                python tools/mkskin.py pack skins/stock/* -o skins.zip
                    each checked, then zipped as skins/<name>/..., which is
                    unpacked onto the card's root as it is.
                python tools/mkskin.py pair skins/stock/c64 -o outdir
                    a skin as the pair the board's Skins file area takes over
                    a YMODEM upload: <name>.txt and <name>.jpg.
                python tools/mkskin.py jpeg art.png -o background.jpg
                    any picture as a background the board decodes: 480 x 320
                    (or --size), baseline, 4:2:0, optimised tables. A picture
                    of another shape is cropped to fill, from its middle,
                    never stretched (--stretch to stretch it).
                python tools/mkskin.py selftest
                    host/skins/cases.txt, the cases the board's own reader
                    is tested with: the two readers must agree on each.

Design:       The grammar here is src/plugins/skin_manifest.h's, rule for
              rule and message for message, and the JPEG check is
              skin_jpeg.h's. selftest is what keeps them the same: both run
              host/skins/cases.txt. The LEDs are drawn as skin_draw.h draws
              them (the same weights and the same screen blend), so a
              preview is what the glass shows, give or take RGB565.

Libraries:    Pillow (check needs none)
Targets:      developer PC, Python 3.8 and later
See also:     SKINS.md, src/plugins/skin_manifest.h, src/plugins/skin_jpeg.h,
              src/plugins/skin_draw.h, tools/mkskins_stock.py

Copyright 2026 - Robert Mech
License:      GNU General Public License v3 or later
SPDX-License-Identifier: GPL-3.0-or-later

This program is free software; you can redistribute it and/or modify it
under the terms of the GNU General Public License as published by the
Free Software Foundation; either version 3 of the License, or (at your
option) any later version.

This program is distributed in the hope that it will be useful, but
WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program. If not, see <https://www.gnu.org/licenses/>. The full
text is in the LICENSE file at the top of this repository.
===========================================================================
"""
import argparse
import math
import os
import re
import sys
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)

# ---------------------------------------------------------------------------
# The format's limits: skin_manifest.h's.
# ---------------------------------------------------------------------------
FORMAT = 1
FILE_MAX = 4096
LINE_MAX = 120
PANEL_MAX = 1024
STRIP_MAX = 16
LED_MIN, LED_MAX, HALO_MAX = 2, 64, 32
NAME_MAX = 24
LINES_MAX = 16
TOKENS_MAX = 20
LED_PIXELS_MAX = 65536
UNIT_PX_MAX = 16384         # the most one widget unit draws at once (kUnitPxMax)
GLYPH_W = (8, 16, 6)            # small, big, tiny: skin_manifest.h's Face order
GLYPH_H = (16, 32, 12)
FACES = ("small", "big", "tiny")
DRIVE_WORDS = ("pc", "1541", "disk2", "breathe")
LINE_WORDS = ("blank", "name", "address", "uptime", "callers", "today", "heap",
              "card", "clock", "date", "last", "ring", "who",
              "online", "lines", "lastcaller", "rssi", "peak", "version")
NUMERIC_WORDS = ("online", "lines", "today", "peak", "rssi", "heap", "clock")
SOURCE_WORDS = ("traffic", "heap", "card", "callers", "rssi")
GRAPH_SOURCES = ("traffic", "callers")
STATE_WORDS = ("rx", "tx", "disk", "run", "closed", "ring", "mail", "staff", "listed", "card", "error",
               "online", "open", "sysop")
NODE_LINES = 16
LABEL_MAX = 12
FIELDS_MAX, DIGITS_MAX, LISTS_MAX, METERS_MAX, GRAPHS_MAX, LAMPS_MAX = 24, 4, 2, 8, 4, 32
DIGITS_N_MAX, DIGITS_H_MIN, DIGITS_H_MAX = 6, 10, 64


class SkinError(Exception):
    """A fault in skin.txt: line 0 means the file as a whole."""

    def __init__(self, line, msg):
        super().__init__(f"line {line}: {msg}" if line else msg)
        self.line = line


class Led:
    def __init__(self, line, x, y, d, halo, colour=(0, 0, 0)):
        self.line, self.x, self.y, self.d, self.halo, self.colour = line, x, y, d, halo, colour

    def box(self):
        side = self.d + 2 * self.halo
        return (self.x - side // 2, self.y - side // 2, side, side)


class Style:
    def __init__(self, fg=(0xC8, 0xC8, 0xC8)):
        self.face = 0
        self.fg = fg
        self.shadow = None
        self.bg = None
        self.align = "left"


class Widget:
    """A field, display, list, meter, graph or lamp: its line, box and the rest."""

    def __init__(self, line, box, **kw):
        self.line, self.box = line, box
        self.__dict__.update(kw)


class Manifest:
    def __init__(self):
        self.w = self.h = 0
        self.name = ""
        self.drive = None
        self.drive_style = 0
        self.activity = None
        self.strip = 0
        self.strip_line = 0
        self.leds = []
        self.text = None
        self.text_line = 0
        self.text_style = Style()
        self.lines = []
        self.lines_line = 0
        self.clock = None
        self.clock_line = 0
        self.clock_style = Style((0xFF, 0xD3, 0x5C))
        self.fields, self.digits, self.nodes, self.events = [], [], [], []
        self.meters, self.graphs, self.lamps = [], [], []


def digit_w(h):
    return h * 11 // 20


def digit_gap(h):
    return max(2, h // 6)


def _number(tok):
    return bool(tok) and len(tok) <= 5 and all("0" <= ch <= "9" for ch in tok)


def _num(line, tok, what, lo, hi):
    if not _number(tok):
        raise SkinError(line, f"{what} '{tok[:16]}' is not a number")
    v = int(tok)
    if v < lo or v > hi:
        raise SkinError(line, f"{what} {v} is outside {lo} to {hi}")
    return v


def _colour(s):
    # Exactly six hex digits: int(x, 16) alone would take a sign ("#+1+2+3"),
    # which the board's reader refuses.
    if not re.fullmatch(r"#[0-9A-Fa-f]{6}", s):
        return None
    return tuple(int(s[1 + 2 * i:3 + 2 * i], 16) for i in range(3))


def _opts(line, d, toks, allowed):
    o = {}
    for t in toks:
        if "=" not in t or t.startswith("="):
            raise SkinError(line, f"{d}: '{t[:16]}' is not an option (name=value)")
        k, v = t.split("=", 1)
        if not v:
            raise SkinError(line, f"{d}: {k[:16]}= has no value")
        kl = k.lower()
        if kl not in allowed:
            raise SkinError(line, f"{d} takes no {k[:16]}= option")
        slot = "colour" if kl in ("colour", "color") else kl
        if slot in o:
            raise SkinError(line, f"{d}: {k[:16]}= is given twice")
        o[slot] = v
    return o


def _led(line, toks, o, colour=(0, 0, 0)):
    x = _num(line, toks[0], "x", 0, PANEL_MAX)
    y = _num(line, toks[1], "y", 0, PANEL_MAX)
    d = _num(line, toks[2], "diameter", LED_MIN, LED_MAX)
    halo = min(d // 2, HALO_MAX)
    if "halo" in o:
        halo = _num(line, o["halo"], "halo", 0, HALO_MAX)
    return Led(line, x, y, d, halo, colour)


def _box(line, toks):
    return (_num(line, toks[0], "x", 0, PANEL_MAX), _num(line, toks[1], "y", 0, PANEL_MAX),
            _num(line, toks[2], "width", 1, PANEL_MAX), _num(line, toks[3], "height", 1, PANEL_MAX))


def _style(line, d, o, s):
    if "size" in o:
        if o["size"].lower() in FACES:
            s.face = FACES.index(o["size"].lower())
        else:
            raise SkinError(line, f"{d}: size={o['size'][:16]} is not tiny, small or big")
    if "colour" in o:
        c = _colour(o["colour"])
        if not c:
            raise SkinError(line, f"{d}: colour={o['colour'][:16]} is not #RRGGBB")
        s.fg = c
    if "shadow" in o:
        if o["shadow"].lower() == "none":
            s.shadow = None
        else:
            c = _colour(o["shadow"])
            if not c:
                raise SkinError(line, f"{d}: shadow={o['shadow'][:16]} is not #RRGGBB or none")
            s.shadow = c
    if "background" in o:
        if o["background"].lower() == "none":
            s.bg = None
        else:
            c = _colour(o["background"])
            if not c:
                raise SkinError(line, f"{d}: background={o['background'][:16]} is not #RRGGBB or none")
            s.bg = c
    if "align" in o:
        a = o["align"].lower()
        if a in ("left", "right"):
            s.align = a
        elif a in ("centre", "center"):
            s.align = "centre"
        else:
            raise SkinError(line, f"{d}: align={o['align'][:16]} is not left, centre or right")


def _fill(line, d, o, fg):
    col, bg = fg, None
    if "colour" in o:
        col = _colour(o["colour"])
        if not col:
            raise SkinError(line, f"{d}: colour={o['colour'][:16]} is not #RRGGBB")
    if "background" in o:
        if o["background"].lower() != "none":
            bg = _colour(o["background"])
            if not bg:
                raise SkinError(line, f"{d}: background={o['background'][:16]} is not #RRGGBB or none")
    return col, bg


def parse(data):
    """skin.txt's bytes into a Manifest, or SkinError."""
    m = Manifest()
    if len(data) > FILE_MAX:
        raise SkinError(0, f"skin.txt is {len(data)} bytes; {FILE_MAX} at most")
    seen_skin = seen_panel = seen_name = False
    raw = data.split(b"\n")
    if raw and raw[-1] == b"":
        raw = raw[:-1]
    for no, b in enumerate(raw, 1):
        if b.endswith(b"\r"):
            b = b[:-1]
        if len(b) > LINE_MAX:
            raise SkinError(no, f"{len(b)} characters; {LINE_MAX} at most")
        for ch in b:
            if ch == 0 or ch == 13 or (ch < 0x20 and ch != 9) or ch > 0x7E:
                raise SkinError(no, f"a character that is not plain ASCII (byte {ch})")
        s = b.decode("ascii")
        p = s.lstrip(" \t")
        if not p or p[0] == "#":
            continue
        if ";" in p:
            p = p[:p.index(";")]
        first = re.match(r"[^ \t]*", p).group(0)
        rest = p[len(first):].strip(" \t")
        toks = [t for t in re.split(r"[ \t]+", p) if t]
        if not toks:
            continue
        if len(toks) > TOKENS_MAX:
            raise SkinError(no, f"more than {TOKENS_MAX} words")
        d, args = toks[0], toks[1:]
        dl = d.lower()

        def want(lo, hi):
            if len(args) < lo:
                raise SkinError(no, f"{d} needs {lo} value{'' if lo == 1 else 's'}")
            if hi >= 0 and len(args) > hi:
                raise SkinError(no, f"{d} has too many values")

        def room(have, most):
            if len(have) >= most:
                raise SkinError(no, f"more than {most} {d} lines")

        if not seen_skin:
            if dl != "skin":
                raise SkinError(no, f"the first line must be 'skin {FORMAT}'")
            want(1, 1)
            if not _number(args[0]):
                raise SkinError(no, f"skin '{args[0][:16]}' is not a number")
            if int(args[0]) != FORMAT:
                raise SkinError(no, f"skin format {int(args[0])}; this board reads format {FORMAT}")
            seen_skin = True
            continue
        if dl == "skin":
            raise SkinError(no, "a second skin line")
        if dl == "panel":
            if seen_panel:
                raise SkinError(no, "a second panel line")
            want(2, 2)
            m.w = _num(no, args[0], "width", 1, PANEL_MAX)
            m.h = _num(no, args[1], "height", 1, PANEL_MAX)
            seen_panel = True
        elif dl == "name":
            if seen_name:
                raise SkinError(no, "a second name line")
            if not rest:
                raise SkinError(no, "name is empty")
            if len(rest) > NAME_MAX:
                raise SkinError(no, f"name is {len(rest)} characters; {NAME_MAX} at most")
            m.name = rest
            seen_name = True
        elif dl == "drive":
            if m.drive:
                raise SkinError(no, f"a second {d} line (the first is line {m.drive.line})")
            if len(args) < 4:
                raise SkinError(no, "drive needs X Y D STYLE")
            o = _opts(no, d, args[4:], ("halo",))
            if args[3].lower() not in DRIVE_WORDS:
                raise SkinError(no, f"drive style '{args[3][:16]}' is not pc, 1541, disk2 or breathe")
            m.drive = _led(no, args, o)
            m.drive_style = DRIVE_WORDS.index(args[3].lower())
        elif dl == "activity":
            if m.activity:
                raise SkinError(no, f"a second {d} line (the first is line {m.activity.line})")
            if len(args) < 3:
                raise SkinError(no, "activity needs X Y D")
            o = _opts(no, d, args[3:], ("halo", "colour", "color"))
            col = (0x5D, 0xDC, 0x7A)
            if "colour" in o:
                col = _colour(o["colour"])
                if not col:
                    raise SkinError(no, f"activity: colour={o['colour'][:16]} is not #RRGGBB")
            m.activity = _led(no, args, o, col)
        elif dl == "strip":
            if m.strip:
                raise SkinError(no, f"a second strip line (the first is line {m.strip_line})")
            want(1, 1)
            m.strip = _num(no, args[0], "strip", 1, STRIP_MAX)
            m.strip_line = no
            m.leds = [None] * m.strip
        elif dl == "led":
            if not m.strip:
                raise SkinError(no, "led before the strip line")
            if len(args) < 4:
                raise SkinError(no, "led needs I X Y D")
            i = _num(no, args[0], "led", 1, m.strip)
            if m.leds[i - 1]:
                raise SkinError(no, f"led {i} again (the first is line {m.leds[i - 1].line})")
            o = _opts(no, d, args[4:], ("halo",))
            m.leds[i - 1] = _led(no, args[1:], o)
        elif dl == "text":
            if m.text:
                raise SkinError(no, f"a second {d} line (the first is line {m.text_line})")
            if len(args) < 4:
                raise SkinError(no, "text needs X Y W H")
            o = _opts(no, d, args[4:], ("size", "colour", "color", "shadow", "background", "align"))
            box = _box(no, args)
            _style(no, d, o, m.text_style)
            m.text = box
            m.text_line = no
        elif dl == "lines":
            if m.lines:
                raise SkinError(no, f"a second lines line (the first is line {m.lines_line})")
            if not args:
                raise SkinError(no, "lines needs at least one word")
            if len(args) > LINES_MAX:
                raise SkinError(no, f"lines has {len(args)} words; {LINES_MAX} at most")
            for k, wd in enumerate(args):
                if wd.lower() not in LINE_WORDS:
                    raise SkinError(no, f"lines: '{wd[:16]}' is not a word the panel knows")
                if wd.lower() == "who" and k != len(args) - 1:
                    raise SkinError(no, "lines: who takes the lines left, so it comes last")
            m.lines = [wd.lower() for wd in args]
            m.lines_line = no
        elif dl == "clock":
            if m.clock:
                raise SkinError(no, f"a second {d} line (the first is line {m.clock_line})")
            if len(args) < 2:
                raise SkinError(no, "clock needs X Y")
            o = _opts(no, d, args[2:], ("size", "colour", "color", "shadow", "background"))
            x = _num(no, args[0], "x", 0, PANEL_MAX)
            y = _num(no, args[1], "y", 0, PANEL_MAX)
            _style(no, d, o, m.clock_style)
            f = m.clock_style.face
            m.clock = (x, y, 5 * GLYPH_W[f], GLYPH_H[f])
            m.clock_line = no
        elif dl == "field":
            room(m.fields, FIELDS_MAX)
            if len(args) < 4:
                raise SkinError(no, "field needs X Y W VALUE")
            o = _opts(no, d, args[4:], ("label", "size", "colour", "color", "shadow", "background", "align"))
            x = _num(no, args[0], "x", 0, PANEL_MAX)
            y = _num(no, args[1], "y", 0, PANEL_MAX)
            w = _num(no, args[2], "width", 1, PANEL_MAX)
            v = args[3].lower()
            if v not in LINE_WORDS or v in ("who", "blank"):
                raise SkinError(no, f"field: '{args[3][:16]}' is not a value a field shows")
            st = Style()
            _style(no, d, o, st)
            label = ""
            if "label" in o:
                if len(o["label"]) > LABEL_MAX:
                    raise SkinError(no, f"field: label is {len(o['label'])} characters; {LABEL_MAX} at most")
                label = o["label"].replace("_", " ")
            if w < GLYPH_W[st.face]:
                raise SkinError(no, f"field is {w} px wide; one character is {GLYPH_W[st.face]}")
            m.fields.append(Widget(no, (x, y, w, GLYPH_H[st.face]), value=v, style=st, label=label))
        elif dl == "digits":
            room(m.digits, DIGITS_MAX)
            if len(args) < 5:
                raise SkinError(no, "digits needs X Y H N VALUE")
            o = _opts(no, d, args[5:], ("colour", "color", "dim"))
            x = _num(no, args[0], "x", 0, PANEL_MAX)
            y = _num(no, args[1], "y", 0, PANEL_MAX)
            h = _num(no, args[2], "height", DIGITS_H_MIN, DIGITS_H_MAX)
            n = _num(no, args[3], "digits", 1, DIGITS_N_MAX)
            v = args[4].lower()
            if v not in NUMERIC_WORDS:
                raise SkinError(no, f"digits: '{args[4][:16]}' is not a number the panel shows")
            if v == "clock" and n != 4:
                raise SkinError(no, "digits: the clock takes 4 digits")
            fg = (0xFF, 0x30, 0x20)
            if "colour" in o:
                fg = _colour(o["colour"])
                if not fg:
                    raise SkinError(no, f"digits: colour={o['colour'][:16]} is not #RRGGBB")
            dim = None
            if "dim" in o and o["dim"].lower() != "none":
                dim = _colour(o["dim"])
                if not dim:
                    raise SkinError(no, f"digits: dim={o['dim'][:16]} is not #RRGGBB or none")
            box = (x, y, n * digit_w(h) + (n - 1) * digit_gap(h), h)
            m.digits.append(Widget(no, box, value=v, n=n, fg=fg, dim=dim))
        elif dl in ("nodes", "events"):
            have = m.nodes if dl == "nodes" else m.events
            room(have, LISTS_MAX)
            if len(args) < 4:
                raise SkinError(no, f"{d} needs X Y W H")
            extra = ("free", "dim", "tint") if dl == "nodes" else ("order", "tint")
            o = _opts(no, d, args[4:], ("size", "colour", "color", "shadow", "background") + extra)
            box = _box(no, args)
            st = Style()
            _style(no, d, o, st)
            free, oldest, tint, dim = False, False, False, None
            if "free" in o:
                if o["free"].lower() not in ("yes", "no"):
                    raise SkinError(no, f"{d}: free={o['free'][:16]} is not yes or no")
                free = o["free"].lower() == "yes"
            if "tint" in o:
                if o["tint"].lower() not in ("yes", "no"):
                    raise SkinError(no, f"{d}: tint={o['tint'][:16]} is not yes or no")
                tint = o["tint"].lower() == "yes"
            if "dim" in o and o["dim"].lower() != "none":
                dim = _colour(o["dim"])
                if not dim:
                    raise SkinError(no, f"nodes: dim={o['dim'][:16]} is not #RRGGBB or none")
            if "order" in o:
                if o["order"].lower() not in ("newest", "oldest"):
                    raise SkinError(no, f"events: order={o['order'][:16]} is not newest or oldest")
                oldest = o["order"].lower() == "oldest"
            gw, gh = GLYPH_W[st.face], GLYPH_H[st.face]
            if box[2] < 8 * gw:
                raise SkinError(no, f"{d} is {box[2]} px wide; 8 characters are {8 * gw}")
            if box[3] < gh:
                raise SkinError(no, f"{d} is {box[3]} px tall; one row is {gh}")
            have.append(Widget(no, box, style=st, free=free, oldest=oldest, tint=tint, dim=dim))
        elif dl in ("meter", "graph"):
            graph = dl == "graph"
            have = m.graphs if graph else m.meters
            room(have, GRAPHS_MAX if graph else METERS_MAX)
            if len(args) < 5:
                raise SkinError(no, f"{d} needs X Y W H SOURCE")
            allowed = ("colour", "color", "background") + (() if graph else ("dir",))
            o = _opts(no, d, args[5:], allowed)
            box = _box(no, args)
            src = args[4].lower()
            if src not in SOURCE_WORDS or (graph and src not in GRAPH_SOURCES):
                raise SkinError(no, f"graph: '{args[4][:16]}' is not traffic or callers" if graph else
                                f"meter: '{args[4][:16]}' is not traffic, heap, card, callers or rssi")
            fg, bg = _fill(no, d, o, (0x5D, 0xDC, 0x7A))
            up = False
            if "dir" in o:
                if o["dir"].lower() not in ("right", "up"):
                    raise SkinError(no, f"meter: dir={o['dir'][:16]} is not right or up")
                up = o["dir"].lower() == "up"
            if box[2] < 2 or box[3] < 2:
                raise SkinError(no, f"{d} is smaller than 2 x 2")
            have.append(Widget(no, box, src=src, fg=fg, bg=bg, up=up))
        elif dl == "lamp":
            room(m.lamps, LAMPS_MAX)
            if len(args) < 4:
                raise SkinError(no, "lamp needs X Y D STATE")
            o = _opts(no, d, args[4:], ("halo", "colour", "color", "blink"))
            blink = False
            if "blink" in o:
                if o["blink"].lower() not in ("yes", "no"):
                    raise SkinError(no, f"lamp: blink={o['blink'][:16]} is not yes or no")
                blink = o["blink"].lower() == "yes"
            st = args[3].lower()
            ok = st in STATE_WORDS
            if not ok and st.startswith("node") and _number(st[4:]):
                ok = 1 <= int(st[4:]) <= NODE_LINES
            if not ok:
                raise SkinError(no, f"lamp: '{args[3][:16]}' is not a state a lamp shows")
            col = (0xFF, 0x2A, 0x10)
            if "colour" in o:
                col = _colour(o["colour"])
                if not col:
                    raise SkinError(no, f"lamp: colour={o['colour'][:16]} is not #RRGGBB")
            led = _led(no, args, o, col)
            led.state = st
            led.blink = blink
            m.lamps.append(led)
        else:
            raise SkinError(no, f"'{d[:16]}' is not a skin directive")

    # The rules that need the whole file.
    if not seen_skin:
        raise SkinError(0, f"empty: the first line must be 'skin {FORMAT}'")
    if not seen_panel:
        raise SkinError(0, "no panel line")
    items = []
    if m.drive:
        items.append((m.drive.box(), "drive", m.drive.line))
    if m.activity:
        items.append((m.activity.box(), "activity", m.activity.line))
    for i, l in enumerate(m.leds):
        if not l:
            raise SkinError(m.strip_line, f"strip {m.strip} but led {i + 1} is missing")
        items.append((l.box(), f"led {i + 1}", l.line))
    for i, l in enumerate(m.lamps):
        items.append((l.box(), f"lamp {i + 1}", l.line))
    led_pixels = sum(b[2] * b[3] for b, _, _ in items)
    if m.text:
        items.append((m.text, "text", m.text_line))
    if m.clock:
        items.append((m.clock, "clock", m.clock_line))
    for kind, have in (("field", m.fields), ("digits", m.digits), ("nodes", m.nodes), ("events", m.events),
                       ("meter", m.meters), ("graph", m.graphs)):
        for i, wd in enumerate(have):
            items.append((wd.box, f"{kind} {i + 1}", wd.line))
    for (b, what, line) in items:
        x, y, w, h = b
        if x < 0 or y < 0 or x + w > m.w or y + h > m.h:
            raise SkinError(line, f"{what}'s box {x},{y} {w}x{h} runs off the {m.w}x{m.h} panel")
    for i in range(len(items)):
        for k in range(i + 1, len(items)):
            a, b = items[i][0], items[k][0]
            if a[0] < b[0] + b[2] and b[0] < a[0] + a[2] and a[1] < b[1] + b[3] and b[1] < a[1] + a[3]:
                raise SkinError(items[k][2], f"{items[k][1]}'s box overlaps {items[i][1]}'s (line {items[i][2]})")
    if led_pixels > LED_PIXELS_MAX:
        raise SkinError(0, f"the LEDs' boxes cover {led_pixels} pixels between them; {LED_PIXELS_MAX} at most")
    # What one unit of each widget draws at once: a row of a list or of the
    # text rectangle is one glyph tall; everything else is its whole box.
    for (b, what, line) in items:
        kind = what.split(" ")[0]
        if kind in ("drive", "activity", "led", "lamp"):
            continue
        px = b[2] * b[3]
        if kind == "text":
            px = b[2] * GLYPH_H[m.text_style.face]
        elif kind in ("nodes", "events"):
            lst = (m.nodes if kind == "nodes" else m.events)[int(what.split(" ")[1]) - 1]
            px = b[2] * GLYPH_H[lst.style.face]
        if px > UNIT_PX_MAX:
            raise SkinError(line, f"{what} draws {px} pixels at once; {UNIT_PX_MAX} at most")
    if m.lines and not m.text:
        raise SkinError(m.lines_line, "lines without a text line")
    if m.text:
        if not m.lines:
            raise SkinError(m.text_line, "text without a lines line")
        f = m.text_style.face
        if m.text[2] < GLYPH_W[f]:
            raise SkinError(m.text_line, f"text is {m.text[2]} px wide; one character is {GLYPH_W[f]}")
        need = len(m.lines) * GLYPH_H[f]
        if need > m.text[3]:
            raise SkinError(m.text_line, f"{len(m.lines)} lines of {GLYPH_H[f]} px need {need} px; "
                                         f"the rectangle is {m.text[3]}")
    return m


# ---------------------------------------------------------------------------
# background.jpg: skin_jpeg.h's rules for the ROM decoder.
# ---------------------------------------------------------------------------
def check_jpeg(data):
    """(width, height) of a JPEG the ESP32-S3's ROM TJpgDec draws, or ValueError."""
    def fail(why):
        raise ValueError(why)

    if data[:2] != b"\xff\xd8":
        fail("not a JPEG (no FF D8 at the start)")
    p = 2
    frame = dqt = dht = False
    w = h = 0
    for _ in range(256):
        if p >= len(data):
            fail("the file ends before its picture")
        if data[p] != 0xFF:
            fail("damaged: a segment does not start with FF")
        p += 1
        if p >= len(data):
            fail("the file ends before its picture")
        mk = data[p]
        p += 1
        if mk == 0xFF:
            fail("fill bytes between segments: save it again as baseline")
        if mk == 0xD8 or mk == 0x01 or 0xD0 <= mk <= 0xD7:
            fail("damaged: a marker out of place before the picture")
        if mk == 0xD9:
            fail("the file ends before its picture")
        if p + 2 > len(data):
            fail("damaged: a segment's length is wrong")
        ln = (data[p] << 8) | data[p + 1]
        if ln < 2:
            fail("damaged: a segment's length is wrong")
        if ln == 2:
            fail("an empty segment the decoder refuses: save it again")
        body = data[p + 2:p + ln]
        blen = ln - 2
        if mk == 0xC0:
            if frame:
                fail("damaged: two frames")
            if blen > 512:
                fail("the frame header is too long for the decoder")
            if len(body) != blen:
                fail("the file ends in its frame header")
            if blen < 6:
                fail("damaged: a frame header too short")
            if body[0] != 8:
                fail("12-bit samples: save it as an ordinary 8-bit JPEG")
            h = (body[1] << 8) | body[2]
            w = (body[3] << 8) | body[4]
            if not w or not h:
                fail("the picture has no size in its header")
            if body[5] == 1:
                fail("greyscale: save it in colour (YCbCr)")
            if body[5] != 3:
                fail("not three colour components (CMYK?): save it as RGB")
            if blen < 15:
                fail("damaged: a frame header too short")
            for i in range(3):
                s, q = body[7 + 3 * i], body[8 + 3 * i]
                if i == 0 and s not in (0x11, 0x21, 0x22):
                    fail("chroma sampling the decoder cannot do: use 4:4:4, 4:2:2 or 4:2:0")
                if i and s != 0x11:
                    fail("chroma sampling the decoder cannot do: use 4:4:4, 4:2:2 or 4:2:0")
                if q > 3:
                    fail("damaged: a quantisation table id past 3")
            frame = True
        elif mk == 0xC2:
            fail("progressive JPEG: save it as baseline (not progressive)")
        elif mk == 0xC1:
            fail("extended JPEG: save it as baseline")
        elif mk in (0xC3, 0xC5, 0xC6, 0xC7, 0xC9, 0xCA, 0xCB, 0xCD, 0xCE, 0xCF):
            fail("a kind of JPEG the decoder cannot do: save it as baseline")
        elif mk in (0xC4, 0xDB):
            if blen > 512:
                fail("a Huffman table segment longer than the decoder's 512 bytes" if mk == 0xC4
                     else "a quantisation segment longer than the decoder's 512 bytes")
            if len(body) != blen:
                fail("the file ends in a table")
            q = 0
            while q < blen:
                tid = body[q]
                if mk == 0xDB:
                    if tid >> 4:
                        fail("16-bit quantisation tables: save it as baseline")
                    if (tid & 15) > 3:
                        fail("damaged: a quantisation table id past 3")
                    q += 65
                    dqt = True
                else:
                    if (tid >> 4) > 1 or (tid & 15) > 1:
                        fail("a Huffman table id the decoder cannot do")
                    if q + 17 > blen:
                        fail("damaged: a Huffman table cut short")
                    q += 17 + sum(body[q + 1:q + 17])
                    dht = True
            if q != blen:
                fail("damaged: a table segment's length is wrong")
        elif mk == 0xDD:
            if blen != 2:
                fail("damaged: a restart interval segment is wrong")
        elif mk == 0xDA:
            if not frame:
                fail("damaged: the picture starts before its frame header")
            if not dqt or not dht:
                fail("no Huffman or quantisation tables before the picture")
            if blen < 1 or len(body) < 1:
                fail("the file ends at its picture")
            if body[0] != 3:
                fail("the picture is not in one scan of three components")
            return w, h
        p += ln
    fail("damaged: too many segments before the picture")


def check_folder(folder):
    """Every fault in a skin folder, as lines of text; [] when it is good."""
    out = []
    name = os.path.basename(os.path.normpath(folder))
    if not re.fullmatch(r"[A-Za-z0-9_-]{1,24}", name) or name.lower() == "status":
        out.append(f"{name}: a skin's folder is 1 to 24 of A-Z a-z 0-9 _ -, and not 'status'")
    txt = os.path.join(folder, "skin.txt")
    jpg = os.path.join(folder, "background.jpg")
    m = None
    try:
        with open(txt, "rb") as f:
            m = parse(f.read())
    except FileNotFoundError:
        out.append("no skin.txt")
    except SkinError as e:
        out.append(f"skin.txt {e}")
    try:
        with open(jpg, "rb") as f:
            w, h = check_jpeg(f.read())
        if m and (w, h) != (m.w, m.h):
            out.append(f"background.jpg is {w}x{h}; skin.txt says the panel is {m.w}x{m.h}")
    except FileNotFoundError:
        out.append("no background.jpg")
    except ValueError as e:
        out.append(f"background.jpg: {e}")
    return out


# ---------------------------------------------------------------------------
# Drawing, as skin_draw.h draws.
# ---------------------------------------------------------------------------
def led_weights(d, halo):
    """side x side of (colour weight, hot weight), 0 to 255: ledWeights()."""
    side = d + 2 * halo
    c = side / 2.0
    r = d / 2.0
    hot_r = r * 0.5
    out = []
    for y in range(side):
        row = []
        for x in range(side):
            e = k = 0.0
            for sy in range(4):
                for sx in range(4):
                    dx = x + (sx + 0.5) / 4.0 - c
                    dy = y + (sy + 0.5) / 4.0 - c
                    t = math.sqrt(dx * dx + dy * dy)
                    if t <= r:
                        u = t / r
                        e += 0.72 + 0.28 * (1.0 - u * u)
                    elif halo > 0 and t < r + halo:
                        u = 1.0 - (t - r) / halo
                        e += 0.5 * u * u
                    if t < hot_r:
                        u = 1.0 - t / hot_r
                        k += u * math.sqrt(u)
            row.append((min(255, int(e / 16.0 * 255.0 + 0.5)), min(255, int(k / 16.0 * 255.0 + 0.5))))
        out.append(row)
    return out


def _div255(x):
    return (x + 128 + ((x + 128) >> 8)) >> 8


def draw_led(px, led, col):
    """One LED lit in col over px (a Pillow pixel access), as drawLed()."""
    x0, y0, side, _ = led.box()
    peak = max(col)
    hot = peak * peak // 255
    w = led_weights(led.d, led.halo)
    for y in range(side):
        for x in range(side):
            e0, k0 = w[y][x]
            if not peak or (not e0 and not k0):
                continue
            a = px[x0 + x, y0 + y]
            k = _div255(k0 * hot)
            outc = []
            for ch in range(3):
                e = _div255(col[ch] * e0)
                e += _div255((255 - e) * k)
                outc.append(255 - _div255((255 - a[ch]) * (255 - e)))
            px[x0 + x, y0 + y] = tuple(outc)


_FONT = None


def font():
    """The panel's Spleen faces from src/plugins/panel_font.h: (small, big, tiny)."""
    global _FONT
    if _FONT is None:
        with open(os.path.join(ROOT, "src", "plugins", "panel_font.h"), encoding="utf-8") as f:
            src = f.read()

        def table(name):
            body = src[src.index(f"constexpr uint8_t {name}["):]
            body = body[body.index("{") + 1:]
            glyphs = []
            for m in re.finditer(r"\{([^{}]*)\}", body):
                glyphs.append([int(v, 16) for v in re.findall(r"0x[0-9A-Fa-f]{2}", m.group(1))])
                if len(glyphs) == 96:
                    break
            return glyphs
        _FONT = (table("kSmall"), table("kBig"), table("kTiny"))
    return _FONT


def glyph_index(ch):
    o = ord(ch)
    if 0x20 <= o < 0x7F:
        return o - 0x20
    if ch == "µ":
        return 95
    return ord("?") - 0x20


def draw_text(px, x, y, s, col, face=0, clip=None):
    """Lit pixels of s at x, y: glyphOver() for each glyph."""
    g = font()[face]
    gw, gh = GLYPH_W[face], GLYPH_H[face]
    bpr = (gw + 7) // 8
    for i, ch in enumerate(s):
        bits = g[glyph_index(ch)]
        for row in range(gh):
            for c in range(gw):
                if (bits[row * bpr + c // 8] >> (7 - c % 8)) & 1:
                    X, Y = x + i * gw + c, y + row
                    if clip and not (clip[0] <= X < clip[0] + clip[2] and clip[1] <= Y < clip[1] + clip[3]):
                        continue
                    px[X, Y] = col


def fill_box(px, box, col):
    x, y, w, h = box
    for yy in range(y, y + h):
        for xx in range(x, x + w):
            px[xx, yy] = col


def text_row(px, box, s, st):
    """One status line in its row box: textRow()."""
    x, y, w, h = box
    if st.bg:
        fill_box(px, box, st.bg)
    gw = GLYPH_W[st.face]
    n = min(len(s), w // gw)
    s = s[:n]
    tw = n * gw
    x0 = x + w - tw if st.align == "right" else x + (w - tw) // 2 if st.align == "centre" else x
    if st.shadow:
        draw_text(px, x0 + 1, y + 1, s, st.shadow, st.face, box)
    draw_text(px, x0, y, s, st.fg, st.face, box)


SAMPLE = {
    "name": "The Rusty Antenna", "address": "192.168.0.40:6400", "uptime": "up 3d 4h",
    "callers": "Callers 3/11", "today": "14 calls today", "heap": "84K free",
    "card": "card 29 GB free", "clock": "21:47", "date": "Sat 26 Sep",
    "last": "21:40 login alice", "ring": "", "blank": "",
    "online": "3", "lines": "11", "lastcaller": "alice", "rssi": "-58 dBm", "peak": "5",
    "version": "1.2.0 (MF35 1.1.0)",
}
SAMPLE_NUM = {"online": 3, "lines": 11, "today": 14, "peak": 5, "rssi": 58, "heap": 84, "clock": 2147}
SAMPLE_WHO = [" 1) alice 7m", " 3) bob 1h", " 4) Wanderer* 2m"]
# Every line: (line, on, mark, handle, doing, on for). Line 0 is the sysop's.
SAMPLE_ROWS = [(0, False, ")", "", "", "")] + [
    (1, True, ")", "alice", "CHAT", "7m"), (2, False, ")", "", "", ""), (3, True, ">", "bob", "FILES", "1h"),
    (4, True, "*", "Wanderer", "MAIL", "2m")] + [(n, False, ")", "", "", "") for n in range(5, 11)]
SAMPLE_EVENTS = ["21:47 login Wanderer", "21:40 login alice", "21:33 logoff carol", "21:20 page bob",
                 "21:02 login bob", "20:51 logoff dave", "20:30 login carol", "20:12 ring dave"]
SAMPLE_FRACTION = {"traffic": 620, "heap": 540, "card": 910, "callers": 272, "rssi": 800}


def node_row(r, cols):
    """nodeRow(): one line of a node list fitted to cols characters."""
    line, on, mark, handle, doing, onfor = r
    label = f"{line:2d}" if line else " S"
    if not on:
        return f"{label}  {'waiting' if cols >= 12 else '-'}"
    rest = cols - 8
    if rest < 4:
        return f"{label}{mark} {handle}"
    dw = min(8, rest - 10) if rest >= 14 else 0
    hw = rest - dw - (1 if dw else 0)
    if dw:
        return f"{label}{mark} {handle[:hw]:<{hw}} {doing[:dw]:<{dw}}{onfor:>4}"
    return f"{label}{mark} {handle[:hw]:<{hw}}{onfor:>4}"


SEGS = (0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F)


def draw_digits(px, d, value):
    """drawDigits(): the seven-segment display lit with value."""
    x0, y0, _, h = d.box
    w, gap = digit_w(h), digit_gap(h)
    t = max(2, h // 7)
    if d.value == "clock":
        text = f"{value // 100:02d}{value % 100:02d}"
    else:
        text = f"{min(value, 10 ** d.n - 1):>{d.n}d}"
    for i, ch in enumerate(text):
        x = x0 + i * (w + gap)
        lit = SEGS[int(ch)] if ch.isdigit() else 0
        seg = [(x + t, y0, w - 2 * t, t), (x + w - t, y0 + t, t, h // 2 - t), (x + w - t, y0 + h // 2, t, h - h // 2 - t),
               (x + t, y0 + h - t, w - 2 * t, t), (x, y0 + h // 2, t, h - h // 2 - t), (x, y0 + t, t, h // 2 - t),
               (x + t, y0 + h // 2 - t // 2, w - 2 * t, t)]
        for s_, box in enumerate(seg):
            if (lit >> s_) & 1:
                fill_box(px, box, d.fg)
            elif d.dim:
                fill_box(px, box, d.dim)
    if d.value == "clock" and d.n == 4:
        cx = x0 + 2 * w + gap + gap // 2 - t // 2
        fill_box(px, (cx, y0 + h // 3 - t // 2, t, t), d.fg)
        fill_box(px, (cx, y0 + 2 * h // 3 - t // 2, t, t), d.fg)


def draw_meter(px, g, frac):
    x, y, w, h = g.box
    if g.bg:
        fill_box(px, g.box, g.bg)
    hi = tuple(c + (255 - c) // 3 for c in g.fg)
    if g.up:
        n = frac * h // 1000
        if n:
            fill_box(px, (x, y + h - n, w, n), g.fg)
            fill_box(px, (x, y + h - n, w, 1), hi)
    else:
        n = frac * w // 1000
        if n:
            fill_box(px, (x, y, n, h), g.fg)
            fill_box(px, (x, y, n, 1), hi)


def draw_graph(px, g):
    import math
    x, y, w, h = g.box
    if g.bg:
        fill_box(px, g.box, g.bg)
    hi = tuple(c + (255 - c) // 2 for c in g.fg)
    for i in range(min(w, 240)):
        if g.src == "callers":
            v = 1000 * (2 + int(2 * math.sin(i / 17.0) + 1.5)) // 11
        else:
            rate = int(900 * (1.2 + math.sin(i / 7.0) + 0.6 * math.sin(i / 2.3)))
            v = min(1000, int(math.log2(max(rate, 0) + 1) / 16 * 1000))
        if v <= 0:
            continue
        hgt = min(h, (v * h + 999) // 1000)
        xx = x + w - 1 - i
        fill_box(px, (xx, y + h - hgt, 1, hgt), g.fg)
        px[xx, y + h - hgt] = hi


def lamp_level(state, i):
    """A lamp's level in the preview: lines 1, 3 and 4 on (3 pressing a key)."""
    if state == "sysop":
        return 0
    if state == "online":
        return 255
    if state == "open":
        return 255
    if state.startswith("node"):
        n = int(state[4:])
        return 255 if n == 3 else 90 if n in (1, 4) else 0
    return {"rx": 255, "tx": 0, "disk": 0, "run": 255, "closed": 0, "ring": 0, "mail": 255, "staff": 255,
            "listed": 255, "card": 255, "error": 0}.get(state, 0)


def preview(folder, strip_colour=(255, 40, 20), pattern=None):
    """The skin as the panel draws it, lit, with sample figures, as a Pillow image."""
    from PIL import Image
    with open(os.path.join(folder, "skin.txt"), "rb") as f:
        m = parse(f.read())
    im = Image.open(os.path.join(folder, "background.jpg")).convert("RGB")
    px = im.load()
    if m.drive:
        draw_led(px, m.drive, (255, 150, 20))          # amber: the card being read
    if m.activity:
        draw_led(px, m.activity, m.activity.colour)
    for i, l in enumerate(m.leds):
        on = pattern[i] if pattern else ((i * 7 + 3) % 5 < 3)
        if on:
            draw_led(px, l, strip_colour)
    for i, l in enumerate(m.lamps):
        lv = lamp_level(l.state, i)
        if lv:
            draw_led(px, l, tuple(c * lv // 255 for c in l.colour))
    if m.text:
        f = m.text_style.face
        gh = GLYPH_H[f]
        rows = m.text[3] // gh
        row = 0
        for wd in m.lines:
            span = rows - row if wd == "who" else 1
            for k in range(span):
                if row >= rows:
                    break
                s = (SAMPLE_WHO[k] if k < len(SAMPLE_WHO) else "") if wd == "who" else SAMPLE[wd]
                text_row(px, (m.text[0], m.text[1] + row * gh, m.text[2], gh), s, m.text_style)
                row += 1
    if m.clock:
        text_row(px, m.clock, SAMPLE["clock"], m.clock_style)
    for fl in m.fields:
        text_row(px, fl.box, fl.label + SAMPLE[fl.value], fl.style)
    for d in m.digits:
        draw_digits(px, d, SAMPLE_NUM[d.value])
    for l in m.nodes:
        gw, gh = GLYPH_W[l.style.face], GLYPH_H[l.style.face]
        if l.style.bg:
            fill_box(px, l.box, l.style.bg)
        rows = [r for r in SAMPLE_ROWS if r[1] or (l.free and r[0])]
        n = l.box[3] // gh
        for k in range(n):
            st = l.style
            if len(rows) > n and k == n - 1:
                s = f" +{len(rows) - (n - 1)} more"
            elif k < len(rows):
                s = node_row(rows[k], l.box[2] // gw)
                if not rows[k][1] and l.dim:
                    st = Style(l.dim)
                    st.face, st.bg, st.shadow, st.align = l.style.face, l.style.bg, l.style.shadow, l.style.align
            else:
                s = ""
            text_row(px, (l.box[0], l.box[1] + k * gh, l.box[2], gh), s, st)
    for l in m.events:
        gh = GLYPH_H[l.style.face]
        if l.style.bg:
            fill_box(px, l.box, l.style.bg)
        n = l.box[3] // gh
        evs = SAMPLE_EVENTS[:n]
        rows = list(reversed(evs)) if l.oldest else evs
        if l.oldest:
            rows = [""] * (n - len(rows)) + rows
        for k in range(n):
            s = rows[k] if k < len(rows) else ""
            text_row(px, (l.box[0], l.box[1] + k * gh, l.box[2], gh), s, l.style)
    for g in m.meters:
        draw_meter(px, g, SAMPLE_FRACTION[g.src])
    for g in m.graphs:
        draw_graph(px, g)
    return im


# ---------------------------------------------------------------------------
# Key colours to skin.txt lines.
# ---------------------------------------------------------------------------
def blobs(im, colour, tol=24):
    """Connected areas of one key colour: [(x0, y0, x1, y1)], x1/y1 exclusive."""
    w, h = im.size
    px = im.load()
    seen = bytearray(w * h)
    out = []

    def near(p):
        return all(abs(p[i] - colour[i]) <= tol for i in range(3))

    for y in range(h):
        for x in range(w):
            if seen[y * w + x] or not near(px[x, y]):
                continue
            stack = [(x, y)]
            seen[y * w + x] = 1
            x0, y0, x1, y1 = x, y, x, y
            while stack:
                cx, cy = stack.pop()
                x0, y0, x1, y1 = min(x0, cx), min(y0, cy), max(x1, cx), max(y1, cy)
                for nx, ny in ((cx + 1, cy), (cx - 1, cy), (cx, cy + 1), (cx, cy - 1)):
                    if 0 <= nx < w and 0 <= ny < h and not seen[ny * w + nx] and near(px[nx, ny]):
                        seen[ny * w + nx] = 1
                        stack.append((nx, ny))
            out.append((x0, y0, x1 + 1, y1 + 1))
    return out


def leds_from_keys(path, keys, style="pc", halo=None):
    from PIL import Image
    im = Image.open(path).convert("RGB")
    lines = [f"skin {FORMAT}", f"panel {im.size[0]} {im.size[1]}"]
    for kind, colour in keys:
        found = blobs(im, colour)
        if not found:
            print(f"mkskin: no {kind} key colour #{colour[0]:02X}{colour[1]:02X}{colour[2]:02X} found",
                  file=sys.stderr)
            continue

        def led(b):
            x0, y0, x1, y1 = b
            cx, cy = (x0 + x1) // 2, (y0 + y1) // 2
            d = max(LED_MIN, min(LED_MAX, max(x1 - x0, y1 - y0)))
            hl = f" halo={halo}" if halo is not None else ""
            return cx, cy, d, hl

        if kind == "drive":
            cx, cy, d, hl = led(found[0])
            lines.append(f"drive {cx} {cy} {d} {style}{hl}")
        elif kind == "activity":
            cx, cy, d, hl = led(found[0])
            lines.append(f"activity {cx} {cy} {d}{hl}")
        elif kind == "led":
            found = found[:STRIP_MAX]
            # Rows first (centres within half an LED of each other), then
            # left to right: how a person numbers a front panel.
            rowed = sorted(found, key=lambda b: ((b[1] + b[3]) // 2, (b[0] + b[2]) // 2))
            rows, cur = [], []
            for b in rowed:
                if cur and abs((b[1] + b[3]) // 2 - (cur[0][1] + cur[0][3]) // 2) > (cur[0][3] - cur[0][1]) // 2:
                    rows.append(cur)
                    cur = []
                cur.append(b)
            if cur:
                rows.append(cur)
            ordered = [b for r in rows for b in sorted(r, key=lambda b: b[0])]
            lines.append(f"strip {len(ordered)}")
            for i, b in enumerate(ordered, 1):
                cx, cy, d, hl = led(b)
                lines.append(f"led {i} {cx} {cy} {d}{hl}")
        elif kind in ("text", "clock"):
            x0, y0, x1, y1 = found[0]
            if kind == "text":
                lines.append(f"text {x0} {y0} {x1 - x0} {y1 - y0}")
                lines.append("lines name address callers who")
            else:
                lines.append(f"clock {x0} {y0}")
    return "\n".join(lines) + "\n"


# ---------------------------------------------------------------------------
# JPEG out, and the zip.
# ---------------------------------------------------------------------------
def fit(im, w, h, stretch=False):
    """im at w x h: cropped from its middle to the shape, then scaled, so
    nothing is stretched; or stretched, when asked."""
    from PIL import Image
    im = im.convert("RGB")
    if im.size == (w, h):
        return im
    if not stretch:
        sw, sh = im.size
        if sw * h > sh * w:                     # wider than the panel: trim the sides
            cw = sh * w // h
            x0 = (sw - cw) // 2
            im = im.crop((x0, 0, x0 + cw, sh))
        else:                                   # taller: trim top and bottom
            ch = sw * h // w
            y0 = (sh - ch) // 2
            im = im.crop((0, y0, sw, y0 + ch))
    return im.resize((w, h), Image.LANCZOS)


def save_jpeg(im, path, quality=86):
    """A background the ROM decoder takes: baseline, 4:2:0, optimised."""
    im.convert("RGB").save(path, "JPEG", quality=quality, subsampling=2, optimize=True, progressive=False)
    with open(path, "rb") as f:
        check_jpeg(f.read())


def pair(folder, outdir):
    """A skin folder as the pair the Skins file area takes: <name>.txt and
    <name>.jpg, checked first."""
    import shutil
    faults = check_folder(folder)
    for x in faults:
        print(f"{folder}: {x}", file=sys.stderr)
    if faults:
        return False
    name = os.path.basename(os.path.normpath(folder))
    os.makedirs(outdir, exist_ok=True)
    shutil.copyfile(os.path.join(folder, "skin.txt"), os.path.join(outdir, name + ".txt"))
    shutil.copyfile(os.path.join(folder, "background.jpg"), os.path.join(outdir, name + ".jpg"))
    return True


def pack(folders, out):
    bad = False
    for fo in folders:
        faults = check_folder(fo)
        for x in faults:
            print(f"{fo}: {x}", file=sys.stderr)
        bad = bad or bool(faults)
    if bad:
        return False
    with zipfile.ZipFile(out, "w") as z:
        for fo in sorted(folders):
            name = os.path.basename(os.path.normpath(fo))
            for fn in ("skin.txt", "background.jpg"):
                z.write(os.path.join(fo, fn), f"skins/{name}/{fn}",
                        compress_type=zipfile.ZIP_DEFLATED if fn.endswith(".txt") else zipfile.ZIP_STORED)
            extra = os.path.join(fo, "README.txt")
            if os.path.exists(extra):
                z.write(extra, f"skins/{name}/README.txt", compress_type=zipfile.ZIP_DEFLATED)
    return True


# ---------------------------------------------------------------------------
# selftest: the cases the board's reader is tested with.
# ---------------------------------------------------------------------------
def _unescape(s):
    out = bytearray()
    i = 0
    b = s.encode("utf-8")
    while i < len(b):
        if b[i] == 0x5C and i + 1 < len(b):
            n = b[i + 1]
            if n == ord("r"):
                out.append(13)
                i += 2
                continue
            if n == ord("t"):
                out.append(9)
                i += 2
                continue
            if n == ord("x") and i + 3 < len(b) and re.fullmatch(rb"[0-9A-Fa-f]{2}", b[i + 2:i + 4]):
                out.append(int(b[i + 2:i + 4], 16))
                i += 4
                continue
        out.append(b[i])
        i += 1
    return bytes(out)


def selftest():
    path = os.path.join(ROOT, "host", "skins", "cases.txt")
    with open(path, encoding="utf-8") as f:
        text = f.read().split("\n")
    if text and text[-1] == "":
        text = text[:-1]
    cases, cur, header = [], None, False
    for line in text:
        if line.startswith("=== "):
            cur = {"name": line[4:], "expect": None, "body": b""}
            cases.append(cur)
            header = True
            continue
        if cur is None:
            continue
        if header:
            header = False
            if line.startswith("expect ok"):
                cur["expect"] = ("ok",)
                continue
            mt = re.match(r"expect error (\d+) ?(.*)", line)
            if mt:
                cur["expect"] = ("error", int(mt.group(1)), mt.group(2))
                continue
        cur["body"] += _unescape(line) + b"\n"
    for c in cases:
        while c["body"].endswith(b"\n\n"):
            c["body"] = c["body"][:-1]
        if c["body"] == b"\n":
            c["body"] = b""
    bad = 0
    for c in cases:
        try:
            parse(c["body"])
            got = ("ok",)
            msg = ""
        except SkinError as e:
            got = ("error", e.line)
            msg = str(e)
        want = c["expect"]
        good = (want[0] == got[0] and (want[0] == "ok" or (want[1] == got[1] and want[2] in msg)))
        if not good:
            bad += 1
            print(f"FAIL {c['name']}: want {want}, got {got} {msg}")
    print(f"mkskin selftest: {len(cases) - bad} of {len(cases)} cases agree with the board's reader")
    return bad == 0


# ---------------------------------------------------------------------------
def main():
    ap = argparse.ArgumentParser(description="µnleashed BBS panel skins: check, make, preview, pack.")
    sub = ap.add_subparsers(dest="cmd", required=True)
    c = sub.add_parser("check", help="check skin folders")
    c.add_argument("folders", nargs="+")
    l_ = sub.add_parser("leds", help="skin.txt lines from key colours painted on a copy of the art")
    l_.add_argument("image")
    l_.add_argument("--key", action="append", default=[],
                    help="kind=#RRGGBB; kind is drive, activity, led, text or clock")
    l_.add_argument("--style", default="pc", choices=DRIVE_WORDS)
    l_.add_argument("--halo", type=int)
    l_.add_argument("-o", "--out", help="write skin.txt here (plain ASCII) rather than to the screen")
    p = sub.add_parser("preview", help="the skin lit, to a PNG")
    p.add_argument("folder")
    p.add_argument("-o", "--out", required=True)
    k = sub.add_parser("pack", help="check, then zip, skin folders")
    k.add_argument("folders", nargs="+")
    k.add_argument("-o", "--out", required=True)
    pr = sub.add_parser("pair", help="a skin as <name>.txt and <name>.jpg, for the Skins file area")
    pr.add_argument("folders", nargs="+")
    pr.add_argument("-o", "--out", required=True)
    j = sub.add_parser("jpeg", help="a picture as a background the board decodes")
    j.add_argument("image")
    j.add_argument("-o", "--out", required=True)
    j.add_argument("--size", default="480x320")
    j.add_argument("--quality", type=int, default=86)
    j.add_argument("--stretch", action="store_true", help="stretch to the size rather than crop to fill")
    sub.add_parser("selftest", help="agree with the board's reader on host/skins/cases.txt")
    a = ap.parse_args()

    if a.cmd == "check":
        bad = False
        for fo in a.folders:
            faults = check_folder(fo)
            print(f"{fo}: {'ok' if not faults else ''}")
            for x in faults:
                print(f"  {x}")
            bad = bad or bool(faults)
        return 1 if bad else 0
    if a.cmd == "leds":
        keys = []
        for kv in a.key:
            kind, _, col = kv.partition("=")
            rgb = _colour(col)
            if kind not in ("drive", "activity", "led", "text", "clock") or not rgb:
                print(f"mkskin: --key {kv}: want kind=#RRGGBB", file=sys.stderr)
                return 2
            keys.append((kind, rgb))
        txt = leds_from_keys(a.image, keys, a.style, a.halo)
        if a.out:
            with open(a.out, "w", encoding="ascii", newline="\n") as f:
                f.write(txt)
        else:
            sys.stdout.write(txt)
        return 0
    if a.cmd == "preview":
        faults = check_folder(a.folder)
        if faults:
            for x in faults:
                print(f"{a.folder}: {x}", file=sys.stderr)
            return 1
        preview(a.folder).save(a.out)
        return 0
    if a.cmd == "pack":
        return 0 if pack(a.folders, a.out) else 1
    if a.cmd == "pair":
        return 0 if all([pair(fo, a.out) for fo in a.folders]) else 1
    if a.cmd == "jpeg":
        from PIL import Image
        w, h = (int(v) for v in a.size.lower().split("x"))
        save_jpeg(fit(Image.open(a.image), w, h, a.stretch), a.out, a.quality)
        return 0
    if a.cmd == "selftest":
        return 0 if selftest() else 1
    return 2


if __name__ == "__main__":
    sys.exit(main())
