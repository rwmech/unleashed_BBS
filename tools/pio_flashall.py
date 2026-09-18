# SPDX-License-Identifier: GPL-2.0-or-later
"""
===========================================================================
 µnleashed BBS
 Electronic freedom on a microcontroller.
===========================================================================

File:         tools/pio_flashall.py
Module:       Tools / PlatformIO extra script

Purpose:      PlatformIO extra script. Adds "pio run -t flashall", which uploads the
              firmware and then the data/ filesystem image. Plain "pio run -t upload"
              is unchanged and leaves every filesystem alone.
              flashall rewrites the storage partition, which is the screens and nothing
              else. The accounts, the live config and each plugin's files are on the
              userdata partition and the caller log is on logs, so neither this nor
              uploadfs touches them. Editing partitions.csv is the exception: moving a
              partition needs a full erase, and takes everything with it.

Libraries:    PlatformIO SCons environment
Targets:      developer PC, PlatformIO
See also:     README.md

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

Import("env")  # noqa: F821  (injected by PlatformIO)

port = env.GetProjectOption("upload_port", "")
port_arg = f" --upload-port {port}" if port else ""

env.AddCustomTarget(  # noqa: F821
    name="flashall",
    dependencies=None,
    actions=[
        f'"$PYTHONEXE" -m platformio run -e $PIOENV -t upload{port_arg}',
        f'"$PYTHONEXE" -m platformio run -e $PIOENV -t uploadfs{port_arg}',
    ],
    title="Flash All",
    description="Upload firmware, then the screens. Accounts and config stay.",
)
