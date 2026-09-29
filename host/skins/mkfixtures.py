"""
===========================================================================
 µnleashed BBS
 Electronic freedom on a microcontroller.
===========================================================================

File:         host/skins/mkfixtures.py
Module:       Host tests / panel skins

Purpose:      The small JPEGs host/test_skin.cpp checks skin::checkJpeg and
              the host's decoder against, made by Pillow the way a sysop's
              paint program would make them: baseline at each chroma
              sampling, optimised tables, full quality, with EXIF, and the
              kinds the ESP32-S3's ROM decoder refuses (progressive,
              greyscale, CMYK), a cut-short file and a PNG. Beside each good
              one, Pillow's own decode as raw RGB (.rgb), which the test
              compares the host decoder's pixels with.

              python host/skins/mkfixtures.py
              Run once; the files are committed.

Libraries:    Pillow
Targets:      developer PC
See also:     host/test_skin.cpp, src/plugins/skin_jpeg.h

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
import io
import os

from PIL import Image

HERE = os.path.join(os.path.dirname(os.path.abspath(__file__)), "jpeg")
W, H = 48, 32


def picture():
    """A gradient with a hard edge, so a wrong decode shows."""
    im = Image.new("RGB", (W, H))
    px = im.load()
    for y in range(H):
        for x in range(W):
            px[x, y] = (x * 255 // (W - 1), y * 255 // (H - 1), 255 if x < W // 2 else 40)
    return im


def save(name, im, raw=True, **kw):
    path = os.path.join(HERE, name)
    im.save(path, "JPEG", **kw)
    if raw:
        with Image.open(path) as back:
            with open(path[:-4] + ".rgb", "wb") as f:
                f.write(back.convert("RGB").tobytes())


def main():
    os.makedirs(HERE, exist_ok=True)
    im = picture()
    save("ok_444.jpg", im, quality=90, subsampling=0)
    save("ok_422.jpg", im, quality=90, subsampling=1)
    save("ok_420.jpg", im, quality=90, subsampling=2)
    save("ok_opt.jpg", im, quality=85, subsampling=2, optimize=True)
    save("ok_q100.jpg", im, quality=100, subsampling=0)
    exif = Image.Exif()
    exif[0x010E] = "a skin with EXIF and a comment"
    save("ok_exif.jpg", im, quality=80, exif=exif.tobytes(), comment=b"made for test_skin")
    save("bad_prog.jpg", im, raw=False, quality=85, progressive=True)
    save("bad_grey.jpg", im.convert("L"), raw=False, quality=85)
    save("bad_cmyk.jpg", im.convert("CMYK"), raw=False, quality=85)
    with open(os.path.join(HERE, "ok_420.jpg"), "rb") as f:
        whole = f.read()
    with open(os.path.join(HERE, "bad_cut.jpg"), "wb") as f:
        f.write(whole[:120])
    # Two things R0.01 refuses that a later TJpgDec takes: a fill byte
    # before a marker, and an empty segment (here an empty comment).
    with open(os.path.join(HERE, "bad_fill.jpg"), "wb") as f:
        f.write(whole[:2] + b"\xff" + whole[2:])
    with open(os.path.join(HERE, "bad_empty.jpg"), "wb") as f:
        f.write(whole[:2] + b"\xff\xfe\x00\x02" + whole[2:])
    buf = io.BytesIO()
    im.save(buf, "PNG")
    with open(os.path.join(HERE, "bad_png.jpg"), "wb") as f:
        f.write(buf.getvalue())
    e2e()


# The skins tools/testclient.py's test_board_s3_skin puts on the host's
# card, for the Waveshare S3's glass with the USB plug up (172 x 320): e2e,
# flat colours a test can read back through PANEL SHOT, and broken, whose
# skin.txt is wrong on line 3.
E2E = os.path.join(os.path.dirname(os.path.abspath(__file__)), "e2e")


def e2e():
    good = os.path.join(E2E, "e2e")
    bad = os.path.join(E2E, "broken")
    os.makedirs(good, exist_ok=True)
    os.makedirs(bad, exist_ok=True)
    im = Image.new("RGB", (172, 320), (32, 64, 96))
    im.paste((96, 48, 32), (0, 160, 172, 320))
    im.save(os.path.join(good, "background.jpg"), "JPEG", quality=95, subsampling=2)
    im.save(os.path.join(bad, "background.jpg"), "JPEG", quality=95, subsampling=2)
    with open(os.path.join(good, "skin.txt"), "w", newline="\n") as f:
        f.write("skin 1\npanel 172 320\nname End to end\n"
                "drive 20 300 10 pc halo=4\nactivity 60 300 10 colour=#00FF00 halo=4\n"
                "strip 2\nled 1 100 300 8\nled 2 130 300 8\n"
                "text 8 8 156 64 colour=#FFFF00 background=#000000\nlines name callers who\n"
                "clock 8 100 colour=#FFFFFF\n")
    with open(os.path.join(bad, "skin.txt"), "w", newline="\n") as f:
        f.write("skin 1\npanel 172 320\ndrive 20 300 10 amiga\n")


if __name__ == "__main__":
    main()
