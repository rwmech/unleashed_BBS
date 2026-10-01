"""
===========================================================================
 µnleashed BBS
 Electronic freedom on a microcontroller.
===========================================================================

File:         host/photos/mkfixtures.py
Module:       Host tests / the panel's new-photo show (1.2.1)

Purpose:      Camera-sized JPEGs for host/test_panel_photo.cpp (the scaler
              at every scale the decoder runs at) and for the host camera
              (BBS_CAM_HOST_JPEG), which test_panel_photo_show files as a
              snap: baseline, at the camera sizes the boards shoot (VGA,
              XGA, HD, UXGA), at 4:2:2 like the sensors' own JPEG and 4:2:0
              like the board's re-encode. A gradient with hard edges, so a
              scaler that lands a row or a column wrong shows. Low quality
              keeps them small; the scaler cares about sizes, not detail.

              python host/photos/mkfixtures.py
              Run once; the files are committed.

Libraries:    Pillow
Targets:      developer PC
See also:     host/test_panel_photo.cpp, src/plugins/panel_photo_fit.h

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
import os

from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))

# name, width, height, Pillow's subsampling (1 is 4:2:2, 2 is 4:2:0)
SIZES = [
    ("vga_422", 640, 480, 1),
    ("xga_420", 1024, 768, 2),
    ("hd_420", 1280, 720, 2),
    ("uxga_422", 1600, 1200, 1),
]


def picture(w, h):
    """A gradient across, another down, and a block every eighth that
    flips, so an edge falls in a different place at every scale."""
    im = Image.new("RGB", (w, h))
    px = im.load()
    for y in range(h):
        for x in range(w):
            block = ((x * 8) // w + (y * 8) // h) % 2
            px[x, y] = (x * 255 // (w - 1), y * 255 // (h - 1), 220 if block else 30)
    return im


def main():
    for name, w, h, sub in SIZES:
        path = os.path.join(HERE, name + ".jpg")
        picture(w, h).save(path, "JPEG", quality=40, subsampling=sub, optimize=False, progressive=False)
        print(f"{path}: {os.path.getsize(path)} bytes")


if __name__ == "__main__":
    main()
