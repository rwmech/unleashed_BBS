#!/usr/bin/env python3
"""
===========================================================================
 µnleashed BBS
 Electronic freedom on a microcontroller.
===========================================================================

File:         tools/mkskins_stock.py
Module:       Tools / panel skins

Purpose:      The stock panel skins, painted: 480 x 320 scenes of the
              machines the board's callers remember, each with its lenses
              where the board lights them and a place for the status lines.
              Each evokes its machine by shape, colour and material, with
              no maker's name, badge or logo anywhere on it.

                python tools/mkskins_stock.py [name ...] [--preview DIR]

              writes skins/stock/<name>/background.jpg and skin.txt, and
              with --preview a lit PNG of each (tools/mkskin.py preview).

Design:       Painted at three times the size and brought down with a
              Lanczos filter, so every edge is antialiased. Each part is a
              shape (a rounded rectangle, a polygon, an ellipse) filled with
              a gradient, lit from the top left: a highlight along its upper
              edges, a shade along its lower ones, a soft shadow under it,
              and for plastic a faint grain. Text on the art (a key's legend,
              a panel's label) is the panel's own Spleen face, so no font
              outside the repository is used.

              A lens is painted unlit: a dark dome of its colour with a rim
              and a pinpoint of reflection. The board lights it by
              screen-blending light over that (skin_draw.h), so where the
              skin.txt says an LED is and where the art has its lens are
              the same numbers, used once here for both.

Libraries:    Pillow, numpy
Targets:      developer PC
See also:     SKINS.md, tools/mkskin.py, skins/stock/

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
import os
import sys

import numpy as np
from PIL import Image, ImageDraw, ImageFilter

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import mkskin  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "skins", "stock")
W, H = 480, 320
S = 3                       # painted at three times the size


def hexc(s):
    s = s.lstrip("#")
    return np.array([int(s[i:i + 2], 16) / 255.0 for i in (0, 2, 4)], dtype=np.float32)


# ---------------------------------------------------------------------------
# The canvas
# ---------------------------------------------------------------------------
class Scene:
    def __init__(self):
        self.a = np.zeros((H * S, W * S, 3), dtype=np.float32)
        self.rng = np.random.default_rng(1541)

    # -- masks, all in final pixels -----------------------------------------
    def _mask(self, draw_fn):
        m = Image.new("L", (W * S, H * S), 0)
        draw_fn(ImageDraw.Draw(m))
        return np.asarray(m, dtype=np.float32) / 255.0

    def rrect(self, x, y, w, h, r=0):
        x0, y0 = int(round(x * S)), int(round(y * S))
        x1, y1 = max(x0, int(round((x + w) * S)) - 1), max(y0, int(round((y + h) * S)) - 1)
        rad = int(max(0, min(r * S, min(x1 - x0, y1 - y0) // 2 - 1)))    # Pillow wants the corners to fit
        if rad < 2:
            return self._mask(lambda d: d.rectangle([x0, y0, x1, y1], fill=255))
        return self._mask(lambda d: d.rounded_rectangle([x0, y0, x1, y1], radius=rad, fill=255))

    def poly(self, pts):
        return self._mask(lambda d: d.polygon([(px * S, py * S) for px, py in pts], fill=255))

    def ellipse(self, cx, cy, rx, ry):
        return self._mask(lambda d: d.ellipse([(cx - rx) * S, (cy - ry) * S, (cx + rx) * S, (cy + ry) * S],
                                              fill=255))

    def blur(self, m, px):
        if px <= 0:
            return m
        im = Image.fromarray((np.clip(m, 0, 1) * 255).astype(np.uint8))
        return np.asarray(im.filter(ImageFilter.GaussianBlur(px * S)), dtype=np.float32) / 255.0

    @staticmethod
    def shift(m, dx, dy):
        o = np.zeros_like(m)
        sx, sy = int(round(dx * S)), int(round(dy * S))
        h, w = m.shape
        xs0, xs1 = max(0, -sx), min(w, w - sx)
        ys0, ys1 = max(0, -sy), min(h, h - sy)
        o[ys0 + sy:ys1 + sy, xs0 + sx:xs1 + sx] = m[ys0:ys1, xs0:xs1]
        return o

    # -- fills ---------------------------------------------------------------
    def vgrad(self, y0, y1, c0, c1):
        """A top to bottom gradient over the whole canvas, c0 at y0, c1 at y1."""
        ys = (np.arange(H * S, dtype=np.float32) / S - y0) / max(1e-3, (y1 - y0))
        t = np.clip(ys, 0, 1)[:, None, None]
        return np.broadcast_to(c0 * (1 - t) + c1 * t, self.a.shape)

    def hgrad(self, x0, x1, c0, c1):
        xs = (np.arange(W * S, dtype=np.float32) / S - x0) / max(1e-3, (x1 - x0))
        t = np.clip(xs, 0, 1)[None, :, None]
        return np.broadcast_to(c0 * (1 - t) + c1 * t, self.a.shape)

    def radial(self, cx, cy, r, c0, c1):
        yy, xx = np.mgrid[0:H * S, 0:W * S].astype(np.float32) / S
        t = np.clip(np.sqrt((xx - cx) ** 2 + (yy - cy) ** 2) / r, 0, 1)[..., None]
        return c0 * (1 - t) + c1 * t

    def paint(self, mask, fill, alpha=1.0):
        m = (np.clip(mask, 0, 1) * alpha)[..., None]
        self.a = self.a * (1 - m) + np.asarray(fill, dtype=np.float32) * m

    def darken(self, mask, amount):
        self.a *= (1 - np.clip(mask, 0, 1) * amount)[..., None]

    def lighten(self, mask, amount, colour=None):
        c = np.ones(3, dtype=np.float32) if colour is None else colour
        m = (np.clip(mask, 0, 1) * amount)[..., None]
        self.a = self.a + (c - self.a) * m

    # -- objects -------------------------------------------------------------
    def shadow(self, mask, dx=3, dy=5, soft=5, amount=0.55):
        self.darken(self.blur(self.shift(mask, dx, dy), soft), amount)

    def bevel(self, mask, width=2.0, light=0.35, dark=0.35, soft=0.6):
        """Light the upper-left edges of a shape and shade the lower-right."""
        inner_tl = mask * (1 - self.shift(mask, width, width))
        inner_br = mask * (1 - self.shift(mask, -width, -width))
        self.lighten(self.blur(inner_tl, soft) * mask, light)
        self.darken(self.blur(inner_br, soft) * mask, dark)

    def inset(self, mask, width=2.0, dark=0.5, light=0.25, soft=0.7):
        """A shape pressed into the surface: shade inside its upper-left rim."""
        inner_tl = mask * (1 - self.shift(mask, width, width))
        inner_br = mask * (1 - self.shift(mask, -width, -width))
        self.darken(self.blur(inner_tl, soft) * mask, dark)
        self.lighten(self.blur(inner_br, soft) * mask, light)

    def grain(self, mask, amount=0.018):
        n = self.rng.normal(0, amount, (H * S, W * S)).astype(np.float32)
        n = self.blur(n * 0.5 + 0.5, 0.35) - 0.5
        self.a += (n * np.clip(mask, 0, 1))[..., None]

    def sheen(self, mask, x0, y0, x1, y1, amount=0.12):
        """A soft broad reflection falling off from (x0,y0) to (x1,y1)."""
        yy, xx = np.mgrid[0:H * S, 0:W * S].astype(np.float32) / S
        dx, dy = x1 - x0, y1 - y0
        t = np.clip(((xx - x0) * dx + (yy - y0) * dy) / (dx * dx + dy * dy), 0, 1)
        self.lighten(mask * (1 - t), amount)

    def slab(self, x, y, w, h, r, top, bottom, shadow=True, bevel=2.0, grain=0.014):
        m = self.rrect(x, y, w, h, r)
        if shadow:
            self.shadow(m)
        self.paint(m, self.vgrad(y, y + h, hexc(top), hexc(bottom)))
        if grain:
            self.grain(m, grain)
        if bevel:
            self.bevel(m, bevel)
        return m

    def lens(self, cx, cy, d, colour, lit=0.0):
        """A panel LED's lens, unlit (lit > 0 paints a lamp that is always on)."""
        r = d / 2.0
        c = hexc(colour)
        rim = self.ellipse(cx, cy, r + 1.2, r + 1.2)
        self.paint(rim, hexc("#15130f"))
        self.bevel(rim, 0.8, 0.25, 0.1)
        m = self.ellipse(cx, cy, r, r)
        self.paint(m, self.radial(cx - r * 0.25, cy - r * 0.25, r * 1.3, c * 0.55, c * 0.16))
        if lit > 0:
            glow = self.ellipse(cx, cy, r * 2.4, r * 2.4)
            self.lighten(self.blur(glow, r * 0.9), 0.45 * lit, c)
            self.paint(m, self.radial(cx, cy, r, np.minimum(1.0, c * 0.4 + 0.75), c), lit)
        hl = self.ellipse(cx - r * 0.35, cy - r * 0.4, r * 0.28, r * 0.2)
        self.lighten(self.blur(hl, 0.3), 0.55)

    def label(self, x, y, text, colour, px=2, big=False, anchor="left", tiny=False):
        """Text in the panel's Spleen face, px canvas pixels a font pixel.
        tiny: the 6 x 12 face, at px=S one glass pixel a font pixel, the
        legend size that can be read on the glass (the tty-ux spec)."""
        face = 2 if tiny else 1 if big else 0
        g = mkskin.font()[face]
        gw, gh = mkskin.GLYPH_W[face], mkskin.GLYPH_H[face]
        bpr = (gw + 7) // 8
        tw = len(text) * gw * px / S
        if anchor == "centre":
            x -= tw / 2
        elif anchor == "right":
            x -= tw
        m = np.zeros((H * S, W * S), dtype=np.float32)
        X0, Y0 = int(round(x * S)), int(round(y * S))
        for i, ch in enumerate(text):
            bits = g[mkskin.glyph_index(ch)]
            for row in range(gh):
                for col in range(gw):
                    if (bits[row * bpr + col // 8] >> (7 - col % 8)) & 1:
                        xx, yy = X0 + (i * gw + col) * px, Y0 + row * px
                        if 0 <= xx < W * S - px and 0 <= yy < H * S - px:
                            m[yy:yy + px, xx:xx + px] = 1.0
        self.paint(self.blur(m, 0.15), hexc(colour))

    def screen(self, x, y, w, h, r, face, edge, curve=0.10):
        """A CRT's glass: the face colour, darker to its edges, a gloss."""
        m = self.rrect(x, y, w, h, r)
        self.inset(m, 2.0, 0.6, 0.15)
        cx, cy = x + w / 2, y + h / 2
        self.paint(m, self.radial(cx, cy, max(w, h) * 0.62, hexc(face), hexc(edge)))
        # the tube's curve: a darker ring just inside the bezel
        ring = m * (1 - self.blur(self.rrect(x + 3, y + 3, w - 6, h - 6, r), 2.5))
        self.darken(ring, 0.35)
        gloss = self.ellipse(x + w * 0.3, y + h * 0.18, w * 0.42, h * 0.16)
        self.lighten(self.blur(gloss, 6) * m, curve)
        return m

    def image(self):
        a = np.clip(self.a, 0, 1)
        im = Image.fromarray((a * 255 + 0.5).astype(np.uint8), "RGB")
        return im.resize((W, H), Image.LANCZOS)


def keyboard(sc, x, y, w, h, rows, key_top, key_side, gap=1.0, special=None, skew=0.0):
    """Rows of keycaps: each a darker skirt and a lighter, dished top."""
    kh = (h - gap * (len(rows) + 1)) / len(rows)
    for ri, row in enumerate(rows):
        total = sum(row)
        unit = (w - gap * (len(row) + 1)) / total
        kx = x + gap + skew * ri
        ky = y + gap + ri * (kh + gap)
        for ci, span in enumerate(row):
            kw = unit * span + gap * (span - 1)
            top, side = key_top, key_side
            if special and (ri, ci) in special:
                top, side = special[(ri, ci)]
            skirt = sc.rrect(kx, ky, kw, kh, 1.4)
            sc.paint(skirt, sc.vgrad(ky, ky + kh, hexc(side), hexc(side) * 0.7))
            cap = sc.rrect(kx + kw * 0.12, ky + kh * 0.08, kw * 0.76, kh * 0.64, 1.2)
            sc.paint(cap, sc.vgrad(ky, ky + kh * 0.7, hexc(top) * 1.08, hexc(top)))
            sc.bevel(cap, 0.6, 0.18, 0.12)
            kx += kw + gap


def desk(sc, y, top="#5a4030", bottom="#2a1c14"):
    """A wooden desk from y down: grain along it, a sheen."""
    m = sc.rrect(0, y, W, H - y, 0)
    sc.paint(m, sc.vgrad(y, H, hexc(top), hexc(bottom)))
    n = sc.rng.normal(0, 1, (H * S, 16)).astype(np.float32)
    grain = np.asarray(Image.fromarray(((n - n.min()) / (np.ptp(n) + 1e-6) * 255).astype(np.uint8))
                       .resize((W * S, H * S), Image.BICUBIC), dtype=np.float32) / 255.0 - 0.5
    sc.a += (grain * 0.10 * m)[..., None]
    sc.sheen(m, W * 0.3, y, W * 0.5, H, 0.08)


def wall(sc, top="#1d2230", bottom="#0e1016"):
    sc.paint(np.ones((H * S, W * S), dtype=np.float32), sc.vgrad(0, H, hexc(top), hexc(bottom)))
    # a lamp's pool of light, up and to the left, and the corners falling off
    sc.lighten(sc.blur(sc.ellipse(120, 40, 220, 120), 30), 0.10, hexc("#ffe8c0"))
    vig = 1 - sc.blur(sc.rrect(-40, -40, W + 80, H + 80, 120), 40)
    sc.darken(vig, 0.5)


def vignette(sc, amount=0.35):
    """The whole scene darker towards its corners, as a lens sees a room."""
    yy, xx = np.mgrid[0:H * S, 0:W * S].astype(np.float32) / S
    d = np.sqrt(((xx - W / 2) / (W * 0.7)) ** 2 + ((yy - H / 2) / (H * 0.8)) ** 2)
    sc.darken(np.clip(d - 0.45, 0, 1) * 1.6, amount)


def modem(sc, x, y, w, h, xs, ly, legy):
    """The BBS's own device, on three of the desks (tty-ux, "The modem"): a
    low charcoal box, a smoked strip, six lamps read across a room. Returns
    the skin.txt lamp lines: MR run, AA open, CD online, RI ring (blinking),
    RD rx, SD tx."""
    body = sc.rrect(x, y, w, h - 3, 4)
    sc.shadow(body, 2, 3, 3, 0.55)
    for fx in (x + 8, x + w - 22):
        sc.paint(sc.rrect(fx, y + h - 5, 14, 5, 1.5), hexc("#0e0e10"))
    sc.paint(body, sc.vgrad(y, y + h - 3, hexc("#3A3A3E"), hexc("#26262A")))
    sc.grain(body, 0.01)
    sc.bevel(body, 1.5, 0.25, 0.35)
    strip = sc.rrect(xs[0] - 10, ly - 6, xs[-1] - xs[0] + 20, legy + 12 - (ly - 6), 2)
    sc.paint(strip, hexc("#141416"))
    sc.inset(strip, 1.0, 0.4, 0.15)
    lines = []
    for lx, word, st in zip(xs, ("MR", "AA", "CD", "RI", "RD", "SD"),
                            ("run", "open", "online", "ring", "rx", "tx")):
        sc.lens(lx, ly, 5, "#ff2410")
        sc.label(lx, legy, word, "#C8C8C0", px=S, tiny=True, anchor="centre")
        lines.append(f"lamp {lx} {ly} 5 {st} colour=#FF3020 halo=4" + (" blink=yes" if st == "ring" else ""))
    return lines


# ---------------------------------------------------------------------------
# Skin 1: a home computer with its disk drive, the breadbin kind, a monitor
# listing who is on, and the modem on the desk.
# ---------------------------------------------------------------------------
def skin_c64():
    sc = Scene()
    wall(sc, "#20222c", "#101118")
    desk(sc, 214, "#4d3a2c", "#241810")

    # The monitor: a brown-beige cabinet, the tube set into it.
    sc.slab(6, 4, 300, 218, 12, "#8d7d68", "#5f5243", bevel=2.5)
    front = sc.rrect(12, 10, 288, 206, 9)
    sc.inset(front, 1.5, 0.3, 0.15)
    # the border ring and the field, the machine's two blues; the field is
    # darker than the first cut so the light blue text holds 6:1 (tty-ux)
    sc.screen(18, 14, 276, 176, 10, "#7a6ad0", "#4a3c98", 0.12)
    field = sc.rrect(32, 26, 248, 152, 4)
    sc.paint(field, sc.radial(156, 102, 170, hexc("#3a2e90"), hexc("#2e2472")))
    sc.lighten(sc.blur(sc.ellipse(120, 44, 90, 18), 8) * field, 0.07)
    scanlines(sc, field, 0.10)
    # the chin: knobs and a power lamp that is simply on
    for kx in (240, 258, 276):
        k = sc.ellipse(kx, 204, 6, 6)
        sc.shadow(k, 1, 1.5, 1.5, 0.5)
        sc.paint(k, sc.radial(kx - 2, 202, 8, hexc("#5a5048"), hexc("#26211c")))
        sc.bevel(k, 1.0, 0.3, 0.3)
    sc.lens(28, 204, 4, "#ff3a20", lit=0.9)
    sc.label(36, 198, "POWER", "#e2d7c0", px=S, tiny=True)

    # The disk drive, narrower, on the right: PWR is the board up, DRV its
    # storage.
    sc.slab(312, 64, 160, 128, 7, "#d8cdb2", "#a99d82", bevel=2.5)
    face = sc.rrect(320, 104, 144, 80, 4)
    sc.paint(face, sc.vgrad(104, 184, hexc("#8a6f55"), hexc("#5d4938")))
    sc.inset(face, 1.5, 0.45, 0.2)
    sc.grain(face, 0.012)
    slot = sc.rrect(338, 124, 108, 9, 3)
    sc.paint(slot, hexc("#0c0908"))
    sc.inset(slot, 1.2, 0.2, 0.25)
    lever = sc.rrect(392, 135, 24, 30, 3)
    sc.shadow(lever, 1.5, 2.5, 2, 0.5)
    sc.paint(lever, sc.vgrad(135, 165, hexc("#3b2e24"), hexc("#1e1712")))
    sc.bevel(lever, 1.2, 0.25, 0.3)
    for vx in range(324, 462, 8):
        v = sc.rrect(vx, 72, 4, 22, 1.5)
        sc.inset(v, 1.0, 0.45, 0.1)
    sc.lens(334, 168, 6, "#40ff50")
    sc.lens(356, 168, 6, "#ff2a1a")
    sc.label(334, 175, "PWR", "#e2d7c0", px=S, tiny=True, anchor="centre")
    sc.label(356, 175, "DRV", "#e2d7c0", px=S, tiny=True, anchor="centre")

    # The computer: the breadbin.
    body = sc.poly([(152, 226), (468, 226), (478, 312), (142, 312)])
    sc.shadow(body, 3, 6, 7, 0.65)
    sc.paint(body, sc.vgrad(226, 312, hexc("#9a8a72"), hexc("#5e5242")))
    sc.grain(body, 0.013)
    sc.bevel(body, 2.2, 0.35, 0.4)
    hump = sc.poly([(156, 226), (464, 226), (467, 244), (153, 244)])
    sc.paint(hump, sc.vgrad(226, 244, hexc("#b3a288"), hexc("#8a7b65")))
    sc.bevel(hump, 1.5, 0.3, 0.2)
    sc.sheen(hump, 200, 226, 260, 244, 0.10)
    well = sc.poly([(158, 247), (430, 247), (433, 309), (155, 309)])
    sc.paint(well, hexc("#2a2119"))
    sc.inset(well, 1.6, 0.5, 0.15)
    rows = [[1] * 16, [1.5] + [1] * 14 + [1.5], [1.8] + [1] * 13 + [2.2], [2.4] + [1] * 11 + [2.6],
            [2.5, 9, 2.5]]
    keyboard(sc, 160, 249, 270, 59, rows, "#5c4a3a", "#2a2018", 1.1, skew=-0.4)
    for i in range(4):
        fk = sc.rrect(438, 248 + i * 15, 26, 13, 1.5)
        sc.shadow(fk, 0.8, 1.2, 1, 0.5)
        sc.paint(fk, sc.vgrad(248 + i * 15, 261 + i * 15, hexc("#d6ccb4"), hexc("#958a74")))
        sc.bevel(fk, 0.8, 0.3, 0.25)
    sc.lens(452, 235, 5, "#ff3020")

    # The modem, on the desk left of the breadbin.
    ml = modem(sc, 8, 258, 128, 46, [30, 48, 66, 84, 102, 120], 278, 288)

    vignette(sc, 0.3)
    m = [
        "skin 1",
        "panel 480 320",
        "name Breadbin and drive",
        "; the monitor: who is on, in the machine's light blue",
        "field 36 30 184 name colour=#C4BAFF",
        "clock 236 30 colour=#C4BAFF",
        "field 36 46 240 address colour=#C4BAFF",
        "field 36 62 240 ring colour=#F0E070",
        "nodes 36 78 240 80 colour=#C4BAFF",
        "field 36 158 104 callers colour=#C4BAFF",
        "field 148 158 128 today colour=#C4BAFF align=right",
        "; the drive: PWR is the board up, DRV its storage",
        "lamp 334 168 6 run colour=#40FF50 halo=5",
        "drive 356 168 6 1541 halo=5",
        "; the computer's power lamp flickers with traffic",
        "activity 452 235 5 colour=#FF3A20 halo=4",
        "; the modem: MR AA CD RI RD SD",
    ] + ml
    return sc.image(), "\n".join(m) + "\n"



def scanlines(sc, mask, amount=0.10):
    lines = np.zeros((H * S, W * S), dtype=np.float32)
    lines[::S * 2, :] = 1.0
    sc.darken(lines * mask, amount)


def segs(sc, x, y, digit, h=14, colour="#ff3020"):
    """A seven-segment digit, lit, at x, y, h tall."""
    w = h * 0.55
    t = h * 0.14
    on = {"0": "abcdef", "1": "bc", "2": "abged", "3": "abgcd", "4": "fgbc", "5": "afgcd",
          "6": "afgedc", "7": "abc", "8": "abcdefg", "9": "abcdfg"}[digit]
    geo = {"a": (x + t, y, w - 2 * t, t), "g": (x + t, y + h / 2 - t / 2, w - 2 * t, t),
           "d": (x + t, y + h - t, w - 2 * t, t), "f": (x, y + t, t, h / 2 - t), "b": (x + w - t, y + t, t, h / 2 - t),
           "e": (x, y + h / 2, t, h / 2 - t), "c": (x + w - t, y + h / 2, t, h / 2 - t)}
    c = hexc(colour)
    for sgn, (gx, gy, gw, gh) in geo.items():
        m = sc.rrect(gx, gy, gw, gh, t / 3)
        if sgn in on:
            sc.lighten(sc.blur(m, 1.2), 0.5, c)
            sc.paint(m, np.minimum(1.0, c * 0.7 + 0.35))
        else:
            sc.paint(m, c * 0.16)


# ---------------------------------------------------------------------------
# Skin 2: a beige desktop: the monitor, the keyboard and the tower with its
# drive bays, its MHz display and the three lamps on the front.
# ---------------------------------------------------------------------------
def skin_pc():
    """The waiting-for-caller screen (tty-ux, internal/tty-ux-skin-widgets-
    2026-09-26.md): the tube is DOS blue with a grey bar top and bottom, the
    lines and the events live on it; the tower's window shows callers on,
    its lamps are power, turbo (traffic) and the disk; the keyboard's lock
    lamps are mail, ring and the board locked to callers."""
    sc = Scene()
    wall(sc, "#262a2e", "#121416")
    top = sc.rrect(0, 238, W, H - 238, 0)
    sc.paint(top, sc.vgrad(238, H, hexc("#6d6a64"), hexc("#34322e")))
    sc.grain(top, 0.02)
    sc.sheen(top, 200, 238, 260, H, 0.07)

    # The monitor: a beige cabinet round a big tube.
    sc.slab(8, 4, 336, 248, 10, "#d9d2c0", "#aaa28e", bevel=2.5)
    bez = sc.rrect(18, 12, 316, 218, 8)
    sc.inset(bez, 2.0, 0.35, 0.2)
    scr = sc.screen(26, 18, 300, 206, 12, "#1428B0", "#0A1460", 0.05)
    scanlines(sc, scr, 0.10)
    # the WFC screen's furniture: the two grey bars, the column heads, a rule
    sc.paint(sc.rrect(30, 22, 292, 20, 0), hexc("#A8A8A8"))
    sc.paint(sc.rrect(30, 199, 292, 17, 0), hexc("#A8A8A8"))
    for x, word in ((34, "Ln"), (66, "Handle"), (202, "Doing")):
        sc.label(x, 43, word, "#55FFFF", px=S, tiny=True)
    sc.label(322, 43, "On", "#55FFFF", px=S, tiny=True, anchor="right")
    sc.paint(sc.rrect(34, 155, 288, 1, 0), hexc("#00AAAA"))
    for kx in (40, 60):
        k = sc.rrect(kx, 236, 12, 8, 2)
        sc.inset(k, 1.0, 0.35, 0.2)
    sc.lens(318, 240, 4, "#40ff50", lit=0.9)

    # The keyboard, with its three lock lamps: MAIL, RING, LOCK.
    kb = sc.poly([(20, 262), (336, 262), (340, 316), (14, 316)])
    sc.shadow(kb, 2, 4, 4, 0.55)
    sc.paint(kb, sc.vgrad(262, 316, hexc("#d6cfbd"), hexc("#a39b87")))
    sc.bevel(kb, 1.8, 0.3, 0.35)
    rows = [[1] * 15, [1.5] + [1] * 13 + [1.5], [1.8] + [1] * 12 + [2.2], [2.3] + [1] * 11 + [2.7],
            [1.5, 1.5, 7, 1.5, 1.5]]
    sp = {(0, 0): ("#9c978a", "#6e6a60"), (1, 0): ("#9c978a", "#6e6a60"), (2, 0): ("#9c978a", "#6e6a60"),
          (3, 0): ("#9c978a", "#6e6a60"), (3, 12): ("#9c978a", "#6e6a60"), (4, 0): ("#9c978a", "#6e6a60"),
          (4, 1): ("#9c978a", "#6e6a60"), (4, 3): ("#9c978a", "#6e6a60"), (4, 4): ("#9c978a", "#6e6a60")}
    keyboard(sc, 22, 266, 266, 46, rows, "#ece6d8", "#b3ac9c", 1.0, sp, skew=-0.5)
    for ly, word in ((276, "MAIL"), (292, "RING"), (308, "LOCK")):
        sc.lens(300, ly, 5, "#40ff50")
        sc.label(308, ly - 6, word, "#6D6656", px=S, tiny=True)

    # The tower: a 5.25-inch bay, the floppy, the callers window, buttons,
    # the three lamps, the key lock, the vents.
    sc.slab(352, 10, 120, 300, 6, "#ded7c5", "#a8a08c", bevel=2.5)
    bay = sc.rrect(360, 20, 104, 26, 2)
    sc.paint(bay, sc.vgrad(20, 46, hexc("#cfc8b5"), hexc("#b3ab97")))
    sc.inset(bay, 1.4, 0.4, 0.2)
    sc.paint(sc.rrect(372, 29, 72, 5, 2), hexc("#0a0a09"))
    sc.lens(452, 40, 3, "#ff3020")
    fl = sc.rrect(368, 54, 88, 18, 2)
    sc.paint(fl, sc.vgrad(54, 72, hexc("#3d3b37"), hexc("#262522")))
    sc.inset(fl, 1.0, 0.3, 0.15)
    sc.paint(sc.rrect(376, 60, 56, 3, 1), hexc("#050505"))
    ej = sc.rrect(436, 64, 12, 5, 1.5)
    sc.paint(ej, hexc("#5a5852"))
    sc.bevel(ej, 0.7, 0.3, 0.3)
    win = sc.rrect(364, 84, 96, 40, 3)
    sc.paint(win, hexc("#140606"))
    sc.inset(win, 1.2, 0.5, 0.15)
    sc.label(412, 128, "CALLERS", "#6D6656", px=S, tiny=True, anchor="centre")
    for i, word in enumerate(("TURBO", "RESET")):
        bx = 364 + i * 52
        b = sc.rrect(bx, 142, 44, 12, 3)
        sc.shadow(b, 1, 1.5, 1.5, 0.5)
        sc.paint(b, sc.vgrad(142, 154, hexc("#c9c1ad"), hexc("#958d7a")))
        sc.bevel(b, 1.0, 0.35, 0.3)
    for word, lx, col in (("POWER", 372, "#40ff50"), ("TURBO", 412, "#ffc020"), ("HDD", 452, "#ff3020")):
        sc.lens(lx, 172, 6, col)
        sc.label(lx, 180, word, "#6D6656", px=S, tiny=True, anchor="centre")
    lock = sc.ellipse(412, 214, 9, 9)
    sc.shadow(lock, 1, 1.5, 1.5, 0.5)
    sc.paint(lock, sc.radial(409, 211, 12, hexc("#e8e6e0"), hexc("#8a8880")))
    sc.bevel(lock, 1.0, 0.3, 0.3)
    sc.paint(sc.rrect(410.5, 208, 3, 12, 1), hexc("#2c2b28"))
    for vy in range(250, 300, 5):
        v = sc.rrect(364, vy, 96, 2.4, 1)
        sc.inset(v, 0.8, 0.5, 0.1)

    vignette(sc, 0.3)
    m = [
        "skin 1",
        "panel 480 320",
        "name Beige tower",
        "; the WFC screen: bar, lines, events, traffic, status",
        "field 34 24 232 name colour=#10106A",
        "clock 282 24 colour=#10106A",
        "nodes 34 56 288 96 colour=#FFFFFF tint=yes",
        "events 34 158 180 36 size=tiny colour=#A8A8A8 order=newest tint=yes",
        "graph 222 158 100 36 traffic colour=#5DDC7A",
        "field 34 202 126 address size=tiny colour=#10106A",
        "field 166 202 90 today size=tiny colour=#10106A",
        "field 262 202 60 uptime size=tiny colour=#10106A align=right",
        "; the tower: callers on in the window, POWER, TURBO (traffic), HDD",
        "digits 378 90 28 2 online colour=#FF3020 dim=#2A0604",
        "lamp 372 172 6 run colour=#40FF50 halo=5",
        "activity 412 172 6 colour=#FFC020 halo=5",
        "drive 452 172 6 pc halo=5",
        "; the keyboard's lock lamps: MAIL, RING, LOCK (closed to callers)",
        "lamp 300 276 5 mail colour=#40FF50 halo=4",
        "lamp 300 292 5 ring colour=#40FF50 halo=4 blink=yes",
        "lamp 300 308 5 closed colour=#40FF50 halo=4",
    ]
    return sc.image(), "\n".join(m) + "\n"


# ---------------------------------------------------------------------------
# Skin 3: the beige one with the lid and the green screen, two floppy drives
# stacked beside it, the modem under them. The screen is the switchboard.
# ---------------------------------------------------------------------------
def skin_apple2():
    sc = Scene()
    wall(sc, "#23201c", "#100e0c")
    desk(sc, 246, "#5a4332", "#281c14")

    # The monitor, on top of the computer, wider for 40 columns.
    sc.slab(30, 4, 300, 196, 10, "#cfc6b0", "#9d947e", bevel=2.5)
    bz = sc.rrect(38, 12, 284, 178, 8)
    sc.inset(bz, 1.8, 0.35, 0.2)
    scr = sc.screen(48, 20, 264, 160, 14, "#0c1a0e", "#030704", 0.09)
    sc.lighten(sc.blur(sc.rrect(62, 34, 236, 132, 8), 14) * scr, 0.06, hexc("#40ff70"))
    scanlines(sc, scr, 0.16)
    sc.lens(312, 195, 3, "#40ff50", lit=0.9)

    # The computer: the lid at the back, the sloping keyboard in front.
    body = sc.poly([(26, 200), (336, 200), (344, 312), (18, 312)])
    sc.shadow(body, 3, 6, 7, 0.6)
    sc.paint(body, sc.vgrad(200, 312, hexc("#d8cfb8"), hexc("#a39983")))
    sc.grain(body, 0.013)
    sc.bevel(body, 2.2, 0.35, 0.4)
    lid = sc.poly([(30, 200), (332, 200), (334, 236), (28, 236)])
    sc.paint(lid, sc.vgrad(200, 236, hexc("#e2d9c3"), hexc("#bdb39c")))
    sc.bevel(lid, 1.5, 0.3, 0.25)
    for vx in range(40, 324, 7):
        v = sc.rrect(vx, 206, 3.5, 12, 1.2)
        sc.inset(v, 0.8, 0.45, 0.1)
    well = sc.poly([(52, 244), (312, 244), (316, 306), (48, 306)])
    sc.paint(well, hexc("#3b3326"))
    sc.inset(well, 1.5, 0.5, 0.15)
    rows = [[1] * 12, [1.5] + [1] * 10 + [1.5], [1.8] + [1] * 10 + [1.2], [2.3] + [1] * 9 + [1.7],
            [2, 7, 2]]
    keyboard(sc, 54, 246, 258, 58, rows, "#3a3632", "#161412", 1.1, skew=-0.3)
    sc.lens(36, 290, 4, "#9cff6a")

    # Two drives, stacked. The top one's IN USE is storage, the bottom one's
    # a card in the slot.
    for i, dy in enumerate((70, 150)):
        sc.slab(352, dy, 118, 72, 5, "#d6cdb6", "#a49a84", bevel=2.2)
        face = sc.rrect(358, dy + 10, 106, 56, 3)
        sc.paint(face, sc.vgrad(dy + 10, dy + 66, hexc("#c7bda6"), hexc("#a9a08a")))
        sc.inset(face, 1.3, 0.35, 0.2)
        slot = sc.rrect(374, dy + 20, 84, 6, 2)
        sc.paint(slot, hexc("#0b0a09"))
        door = sc.rrect(384, dy + 30, 64, 18, 2)
        sc.shadow(door, 1, 2, 2, 0.45)
        sc.paint(door, sc.vgrad(dy + 30, dy + 48, hexc("#d9d0ba"), hexc("#a39a84")))
        sc.bevel(door, 1.2, 0.35, 0.3)
        sc.lens(368, dy + 56, 5, "#ff2a18")
        sc.label(376, dy + 51, "IN USE", "#5e5646", px=S, tiny=True)
    base = sc.rrect(348, 226, 126, 20, 3)
    sc.shadow(base, 2, 4, 4, 0.6)
    sc.paint(base, sc.vgrad(226, 246, hexc("#6f6758"), hexc("#3e3930")))
    sc.bevel(base, 1.2, 0.2, 0.3)

    # The modem on the desk, under the drives' stand.
    ml = modem(sc, 352, 256, 120, 44, [372, 389, 406, 423, 440, 457], 274, 283)

    vignette(sc, 0.32)
    m = [
        "skin 1",
        "panel 480 320",
        "name Beige lid and drives",
        "; the green screen: every line, taken or waiting",
        "field 60 26 108 name size=tiny colour=#4CFF7A",
        "field 174 26 126 address size=tiny colour=#4CFF7A align=right",
        "nodes 60 40 240 132 size=tiny colour=#4CFF7A free=yes dim=#2A9A48",
        "; the drives: the top one's IN USE is storage, the bottom one's a card in",
        "drive 368 126 5 disk2 halo=5",
        "lamp 368 206 5 card colour=#FF2A18 halo=5",
        "; the power lamp on the keyboard, for traffic",
        "activity 36 290 4 colour=#9CFF6A halo=4",
        "; the modem: MR AA CD RI RD SD",
    ] + ml
    return sc.image(), "\n".join(m) + "\n"


# ---------------------------------------------------------------------------
# Skin 4: the cream computer with the dark keyboard and gold function keys,
# its disk drive with the modem on top, and a wood-grain television
# scrolling the board's events.
# ---------------------------------------------------------------------------
def skin_atari():
    sc = Scene()
    wall(sc, "#2a2622", "#12100e")
    desk(sc, 222, "#3d2d22", "#1c140e")

    # The television: a wood cabinet, the screen, a panel of dials.
    tv = sc.slab(10, 8, 280, 206, 12, "#6b4a2e", "#3b2715", bevel=2.5)
    n = sc.rng.normal(0, 1, (8, W * S)).astype(np.float32)
    grain = np.asarray(Image.fromarray(((n - n.min()) / (np.ptp(n) + 1e-6) * 255).astype(np.uint8))
                       .resize((W * S, H * S), Image.BICUBIC), dtype=np.float32) / 255.0 - 0.5
    sc.a += (grain * 0.16 * tv)[..., None]
    sc.sheen(tv, 60, 8, 120, 214, 0.08)
    mask = sc.rrect(18, 18, 216, 186, 10)
    sc.paint(mask, hexc("#1a1612"))
    sc.inset(mask, 1.6, 0.4, 0.15)
    # a deeper blue than the first cut, so the text holds 7:1 (tty-ux)
    scr = sc.screen(24, 24, 208, 172, 20, "#1c4a94", "#0e2650", 0.13)
    scanlines(sc, scr, 0.10)
    side = sc.rrect(238, 22, 44, 180, 6)
    sc.paint(side, sc.vgrad(22, 202, hexc("#2a2622"), hexc("#171411")))
    sc.inset(side, 1.4, 0.35, 0.15)
    for ky in (50, 92):
        k = sc.ellipse(260, ky, 12, 12)
        sc.shadow(k, 1, 2, 2, 0.5)
        sc.paint(k, sc.radial(256, ky - 4, 16, hexc("#9a9690"), hexc("#3c3a36")))
        sc.bevel(k, 1.2, 0.35, 0.35)
        sc.paint(sc.rrect(259, ky - 10, 2, 8, 1), hexc("#1a1917"))
    # the channel readout (callers on) and the tuning meter's slot (signal)
    win = sc.rrect(244, 118, 32, 32, 3)
    sc.paint(win, hexc("#140606"))
    sc.inset(win, 1.2, 0.5, 0.15)
    slot = sc.rrect(252, 158, 16, 38, 2)
    sc.paint(slot, hexc("#0a0908"))
    sc.inset(slot, 1.2, 0.5, 0.15)

    # The disk drive: cream top, a dark front with the door and two lamps.
    sc.slab(302, 60, 168, 140, 7, "#e6dcc4", "#b3a88e", bevel=2.5)
    front = sc.rrect(310, 104, 152, 90, 4)
    sc.paint(front, sc.vgrad(104, 194, hexc("#3a2b20"), hexc("#20170f")))
    sc.inset(front, 1.5, 0.45, 0.2)
    sc.grain(front, 0.012)
    slot = sc.rrect(330, 124, 116, 7, 2.5)
    sc.paint(slot, hexc("#050403"))
    sc.inset(slot, 1.0, 0.2, 0.3)
    lever = sc.rrect(368, 136, 40, 12, 3)
    sc.shadow(lever, 1, 2, 2, 0.5)
    sc.paint(lever, sc.vgrad(136, 148, hexc("#c9bda3"), hexc("#8c826d")))
    sc.bevel(lever, 1.0, 0.35, 0.3)
    sc.lens(330, 170, 5, "#ff2a18")                        # PWR: run
    sc.lens(350, 170, 5, "#ff2a18")                        # BUSY: the drive light
    sc.label(328, 178, "PWR", "#b8ab90", px=S, tiny=True, anchor="centre")
    sc.label(356, 178, "BUSY", "#b8ab90", px=S, tiny=True, anchor="centre")
    for vx in range(314, 460, 9):
        v = sc.rrect(vx, 72, 4.5, 24, 1.6)
        sc.inset(v, 1.0, 0.45, 0.1)

    # The modem, sitting on the drive.
    ml = modem(sc, 312, 18, 148, 42, [336, 356, 376, 396, 416, 436], 36, 44)

    # The computer: a cream wedge, the dark keyboard, the gold keys.
    body = sc.poly([(90, 226), (472, 226), (478, 312), (82, 312)])
    sc.shadow(body, 3, 6, 7, 0.6)
    sc.paint(body, sc.vgrad(226, 312, hexc("#e8dec6"), hexc("#b2a78e")))
    sc.grain(body, 0.012)
    sc.bevel(body, 2.2, 0.35, 0.4)
    well = sc.poly([(100, 240), (416, 240), (419, 306), (97, 306)])
    sc.paint(well, sc.vgrad(240, 306, hexc("#3b2c22"), hexc("#241a13")))
    sc.inset(well, 1.5, 0.5, 0.15)
    rows = [[1] * 14, [1.5] + [1] * 12 + [1.5], [1.8] + [1] * 11 + [2.2], [2.4] + [1] * 10 + [2.6],
            [3, 8, 3]]
    keyboard(sc, 102, 242, 314, 62, rows, "#4a3a2e", "#211811", 1.1, skew=-0.3)
    for i in range(4):
        fk = sc.rrect(428, 244 + i * 15, 38, 12, 2)
        sc.shadow(fk, 0.8, 1.2, 1, 0.5)
        sc.paint(fk, sc.vgrad(244 + i * 15, 256 + i * 15, hexc("#e8b64a"), hexc("#a8781e")))
        sc.bevel(fk, 0.8, 0.35, 0.25)
    sc.lens(460, 233, 4, "#ff3020")

    vignette(sc, 0.32)
    m = [
        "skin 1",
        "panel 480 320",
        "name Cream and wood",
        "; the television: the board's name and the events, newest at the bottom",
        "field 34 30 144 name colour=#E8F0FF shadow=#0A1A38",
        "clock 184 30 colour=#E8F0FF shadow=#0A1A38",
        "events 34 50 192 128 order=oldest colour=#E8F0FF shadow=#0A1A38",
        "field 42 180 176 address size=tiny colour=#E8F0FF align=centre",
        "; the channel readout is callers on, the tuning meter is the signal",
        "digits 245 122 24 2 online colour=#FF3020 dim=#2A0804",
        "meter 254 160 12 34 rssi colour=#5DDC7A dir=up",
        "; the drive: PWR, BUSY; the computer's power lamp for traffic",
        "lamp 330 170 5 run colour=#FF2A18 halo=5",
        "drive 350 170 5 1541 halo=5",
        "activity 460 233 4 colour=#FF3A20 halo=4",
        "; the modem on the drive: MR AA CD RI RD SD",
    ] + ml
    return sc.image(), "\n".join(m) + "\n"


# ---------------------------------------------------------------------------
# Skin 5: a front panel whose lamps mean something: status, the strip on
# PROGRAMMED OUTPUT, the board on DATA, the lines on the address row, and
# the events on green-bar paper coming out over the top.
# ---------------------------------------------------------------------------
IMSAI_ADDR_X = [30 + i * 26 + (10 if i >= 8 else 0) for i in range(16)]
IMSAI_ADDR_Y = 196
IMSAI_OUT_X = [30 + i * 26 + (10 if i >= 4 else 0) for i in range(8)]
IMSAI_DATA_X = [262 + i * 26 for i in range(8)]


def paddle(sc, x, y, colour, up=False):
    """A long paddle switch in its bushing: the body, lit from the top."""
    c = hexc(colour)
    bush = sc.ellipse(x, y, 4.5, 4.5)
    sc.paint(bush, sc.radial(x - 1, y - 1, 6, hexc("#d8d8d8"), hexc("#5a5a5a")))
    top, bot = (y - 26, y + 2) if up else (y - 2, y + 26)
    p = sc.poly([(x - 7, top + 4), (x + 7, top + 4), (x + 8, bot), (x - 8, bot)])
    p = np.maximum(p, sc.rrect(x - 7, top, 14, 10, 3))
    sc.shadow(p, 2, 3, 2.5, 0.55)
    sc.paint(p, sc.hgrad(x - 8, x + 8, c * 1.1, c * 0.62))
    sc.bevel(p, 1.2, 0.35, 0.35)
    sc.lighten(sc.blur(sc.rrect(x - 4, top + 2, 3, (bot - top) - 6, 1.5), 0.6) * p, 0.35)


def skin_imsai():
    sc = Scene()
    ink = "#e6ecf4"
    sc.paint(np.ones((H * S, W * S), dtype=np.float32), sc.vgrad(0, H, hexc("#3a3d42"), hexc("#16181b")))
    panel = sc.slab(6, 6, 468, 308, 6, "#4f6f9e", "#34507a", bevel=2.5, grain=0.01)
    sc.sheen(panel, 60, 6, 200, 200, 0.10)
    for ry in (106, 178, 250):
        sc.paint(sc.rrect(16, ry, 448, 1.2, 0), hexc("#d8e0ec"), 0.55)

    # The readout: a VFD's teal behind the glass, top right.
    win = sc.rrect(264, 12, 206, 54, 4)
    sc.paint(win, hexc("#061210"))
    sc.inset(win, 2.0, 0.6, 0.15)
    sc.lighten(sc.blur(sc.rrect(270, 16, 194, 46, 3), 6) * win, 0.10, hexc("#30ffd0"))
    grid = np.zeros((H * S, W * S), dtype=np.float32)
    grid[::S, :] = 1.0
    grid[:, ::S] = 1.0
    sc.darken(grid * win, 0.12)
    sc.lighten(sc.blur(sc.ellipse(340, 18, 70, 6), 4) * win, 0.06)

    # The status row: RUN WAIT INT HLTA INP OUT DISK MAIL.
    status = ["RUN", "WAIT", "INT", "HLTA", "INP", "OUT", "DISK", "MAIL"]
    for i, word in enumerate(status):
        lx = 272 + i * 27
        sc.lens(lx, 78, 8, "#ff2410")
        sc.label(lx, 87, word, ink, px=S, tiny=True, anchor="centre")

    # PROGRAMMED OUTPUT (the strip) and DATA (the board).
    sc.label(16, 112, "PROGRAMMED OUTPUT", ink, px=S, tiny=True)
    for i, lx in enumerate(IMSAI_OUT_X):
        sc.lens(lx, 136, 8, "#ff2410")
        sc.label(lx, 145, str(7 - i), ink, px=S, tiny=True, anchor="centre")
    sc.label(256, 112, "DATA", ink, px=S, tiny=True)
    for i, lx in enumerate(IMSAI_DATA_X):
        sc.lens(lx, 136, 8, "#ff2410")
        word = {5: "OPS", 6: "DIR", 7: "CARD"}.get(i, str(7 - i))
        sc.label(lx, 145, word, ink, px=S, tiny=True, anchor="centre")

    # LINES: the address row, line n on bit n-1, the sysop's on A15.
    sc.label(16, 184, "LINES", ink, px=S, tiny=True)
    for i, lx in enumerate(IMSAI_ADDR_X):
        sc.lens(lx, IMSAI_ADDR_Y, 8, "#ff2410")
        bit = 15 - i
        word = "SYS" if bit == 15 else str(bit + 1) if bit <= 9 else ""
        if word:
            sc.label(lx, IMSAI_ADDR_Y + 9, word, ink, px=S, tiny=True, anchor="centre")

    # The paddles and the controls.
    for i, lx in enumerate(IMSAI_ADDR_X):
        grp = (15 - i) // 3
        paddle(sc, lx, 226, "#2f5fd0" if grp % 2 == 0 else "#d03022", up=(i in (1, 3)))
    for i, word in enumerate(["STOP", "RUN", "STEP", "EXAM", "DEP", "RESET"]):
        cx = 40 + i * 44
        paddle(sc, cx, 270, "#d03022" if i % 2 else "#2f5fd0")
        sc.label(cx, 300, word, ink, px=S, tiny=True, anchor="centre")
    lock = sc.ellipse(360, 280, 12, 12)
    sc.shadow(lock, 1.5, 2, 2, 0.5)
    sc.paint(lock, sc.radial(356, 276, 16, hexc("#eeeeee"), hexc("#7a7a7a")))
    sc.bevel(lock, 1.2, 0.3, 0.3)
    sc.paint(sc.rrect(358, 271, 4, 18, 1.5), hexc("#222222"))
    rock = sc.rrect(410, 266, 44, 30, 4)
    sc.shadow(rock, 1.5, 2.5, 2.5, 0.55)
    sc.paint(rock, sc.vgrad(266, 296, hexc("#2a2a2a"), hexc("#0c0c0c")))
    sc.bevel(rock, 1.2, 0.3, 0.3)
    sc.lens(432, 258, 4, "#ff2410", lit=0.9)
    sc.label(432, 300, "POWER", ink, px=S, tiny=True, anchor="centre")

    # The printout: green-bar paper hanging over the top left, torn off.
    rng = np.random.default_rng(64)
    tear = [(12 + k * 4, 100 + rng.uniform(0, 5)) for k in range(62)]
    paper = sc.poly([(12, 0), (256, 0), (256, tear[-1][1])] + tear[::-1])
    sc.shadow(paper, 2, 4, 4, 0.5)
    sc.paint(paper, hexc("#EEF0E4"))
    for by in (6, 78):
        sc.paint(sc.rrect(24, by, 220, 36, 0) * paper, hexc("#CDE6C4"))
    for tx in (12, 244):
        sc.paint(sc.rrect(tx, 0, 12, 110, 0) * paper, hexc("#E6E8DC"))
        for hy in range(6, 104, 12):
            hole = sc.ellipse(tx + 6, hy, 2.4, 2.4) * paper
            sc.paint(hole, hexc("#2a3446"))
    for px_ in (24, 244):
        sc.paint(sc.rrect(px_, 0, 0.6, 110, 0) * paper, hexc("#b8baa8"))
    sc.darken(sc.blur(sc.rrect(12, 0, 244, 3, 0), 1.5) * paper, 0.15)

    vignette(sc, 0.2)
    m = [
        "skin 1",
        "panel 480 320",
        "name Front panel",
        "; the printout: events, newest at the bottom",
        "events 28 6 210 84 size=tiny colour=#2A2A30 order=oldest",
        "; the readout",
        "field 270 16 144 name colour=#6CFFE0",
        "clock 424 16 colour=#6CFFE0",
        "field 270 32 194 address colour=#6CFFE0",
        "field 270 48 194 callers colour=#6CFFE0",
        "; status: RUN WAIT INT HLTA INP OUT DISK MAIL",
        "lamp 272 78 8 run colour=#FF2410 halo=5",
        "lamp 299 78 8 closed colour=#FF2410 halo=5",
        "lamp 326 78 8 ring colour=#FF2410 halo=5 blink=yes",
        "lamp 353 78 8 error colour=#FF2410 halo=5 blink=yes",
        "lamp 380 78 8 rx colour=#FF2410 halo=5",
        "lamp 407 78 8 tx colour=#FF2410 halo=5",
        "drive 434 78 8 1541 halo=5",
        "lamp 461 78 8 mail colour=#FF2410 halo=5",
        "; programmed output: the lights plugin's strip, pixels 1 to 8",
        "; (the strip's pixels 9 and 10 have no lamp here)",
        "strip 8",
    ]
    for i, lx in enumerate(IMSAI_OUT_X):
        m.append(f"led {i + 1} {lx} 136 8 halo=5")
    m += [
        "; data: OPS DIR CARD on D2 D1 D0; D7 to D3 dark",
        f"lamp {IMSAI_DATA_X[5]} 136 8 staff colour=#FF2410 halo=5",
        f"lamp {IMSAI_DATA_X[6]} 136 8 listed colour=#FF2410 halo=5",
        f"lamp {IMSAI_DATA_X[7]} 136 8 card colour=#FF2410 halo=5",
        "; the lines, line n on address bit n-1, the sysop's on A15",
    ]
    for n in range(1, 11):
        m.append(f"lamp {IMSAI_ADDR_X[16 - n]} {IMSAI_ADDR_Y} 8 node{n} colour=#FF2410 halo=5")
    m.append(f"lamp {IMSAI_ADDR_X[0]} {IMSAI_ADDR_Y} 8 sysop colour=#FF2410 halo=5")
    return sc.image(), "\n".join(m) + "\n"


SKINS = {
    "c64": skin_c64,
    "pc": skin_pc,
    "apple2": skin_apple2,
    "atari": skin_atari,
    "imsai": skin_imsai,
}


def build(name, preview_dir=None):
    im, txt = SKINS[name]()
    folder = os.path.join(OUT, name)
    os.makedirs(folder, exist_ok=True)
    mkskin.save_jpeg(im, os.path.join(folder, "background.jpg"))
    with open(os.path.join(folder, "skin.txt"), "w", newline="\n", encoding="ascii") as f:
        f.write(txt)
    faults = mkskin.check_folder(folder)
    if faults:
        raise SystemExit(f"{name}: " + "; ".join(faults))
    size = os.path.getsize(os.path.join(folder, "background.jpg"))
    print(f"{name}: {size} bytes")
    if preview_dir:
        os.makedirs(preview_dir, exist_ok=True)
        mkskin.preview(folder).save(os.path.join(preview_dir, f"{name}.png"))


def main():
    ap = argparse.ArgumentParser(description="Paint the stock panel skins.")
    ap.add_argument("names", nargs="*")
    ap.add_argument("--preview")
    a = ap.parse_args()
    for n in a.names or list(SKINS):
        build(n, a.preview)
    return 0


if __name__ == "__main__":
    sys.exit(main())
