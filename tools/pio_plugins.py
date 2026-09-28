# SPDX-License-Identifier: GPL-3.0-or-later
"""
===========================================================================
 µnleashed BBS
 Electronic freedom on a microcontroller.
===========================================================================

File:         tools/pio_plugins.py
Module:       Tools / PlatformIO pre-script for external plugins (1.2.0)

Purpose:      Before PlatformIO builds an environment, fetch the plugins its
              custom_ext_plugins names (tools/plugins.py, at the commits
              plugins.lock pins) and hand the list to src/CMakeLists.txt, so
              `pio run -e <env>` needs nothing typed beforehand.

              The list reaches CMake two ways, because PlatformIO's ESP-IDF
              builder runs CMake as a child of this process and caches its
              configuration: as BBS_EXT_PLUGINS in the environment, and as a
              file in the build directory that CMake lists as a configure
              dependency, so a changed list reconfigures the build.

              An environment with no custom_ext_plugins does nothing here.

Targets:      developer PC, GitHub Actions
See also:     tools/plugins.py, src/CMakeLists.txt, LINK.md

Copyright 2026 - Robert Mech
License:      GNU General Public License v3 or later
SPDX-License-Identifier: GPL-3.0-or-later
===========================================================================
"""
import os
import pathlib
import subprocess
import sys

Import("env")  # noqa: F821  (PlatformIO's SCons global)

names = env.GetProjectOption("custom_ext_plugins", "").split()  # noqa: F821
root = pathlib.Path(env.subst("$PROJECT_DIR"))  # noqa: F821
build = pathlib.Path(env.subst("$BUILD_DIR"))  # noqa: F821

if names:
    r = subprocess.run([sys.executable, str(root / "tools" / "plugins.py"), "fetch", *names], cwd=root)
    if r.returncode:
        sys.exit(r.returncode)

os.environ["BBS_EXT_PLUGINS"] = ";".join(names)
build.mkdir(parents=True, exist_ok=True)
listing = build / "ext_plugins.txt"
text = ";".join(names)
if not listing.exists() or listing.read_text() != text:
    listing.write_text(text)
