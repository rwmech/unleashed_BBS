#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""
===========================================================================
 µnleashed BBS
 Electronic freedom on a microcontroller.
===========================================================================

File:         tools/release.py
Module:       Tools / release build

Purpose:      Builds a public release: the five flash images the web
              installer writes, for each chip family a release carries, the
              third-party licence notices, and a SHA256SUMS over all of it.
              Run by the GitHub Action on a version tag, and by hand to test
              a release before tagging.

              Nine builds (BUILDS below): the ESP32, the reference
              WROOM-32E; the ESP32-S3, built for the Waveshare
              ESP32-S3-LCD-1.47 profile; the Freenove ESP32-WROVER CAM, a
              second ESP32 image (1.1.0); and the AI-Thinker ESP32-CAM, a
              third (1.1.1). A board profile is a build, so another board is
              another row with its own directory. Four more, the 1.1.2
              hardware preview's boards (the Waveshare
              ESP32-S3-Touch-LCD-4.3B, ESP32-S3-Touch-LCD-2 and ESP32-S3-ETH,
              and the Makerfabs Parallel TFT 3.5"), were tag_only until they
              merged into 1.2.0 and are ordinary sets since, and the
              Makerfabs v2.0 (1.2.1) joined them: nine in all.

Output:       release/<version>/assets/    flat, for a GitHub Release, the
                                           shape deploy/fetch_release.py in
                                           the directory repository fetches.
                                           The ESP32's parts keep their plain
                                           names, as every earlier release had
                                           them; another family's carry its
                                           directory as a prefix:
                                           esp32s3-firmware.bin. Each family
                                           also has version.txt (the S3's as
                                           esp32s3-version.txt): one line, the
                                           version as that board shows it
              release/<version>/install/   the directory server's own layout,
                                           <family>/<parts> (esp32/, esp32s3/,
                                           esp32-fncam/, esp32-cam/)
                                           with version.txt, for copying
                                           straight into firmware/<version>/ by
                                           hand, and in each family's folder a
                                           manifest.json of its own, for trying
                                           the images in ESP Web Tools before
                                           the directory serves them

Versions:     The core version is BBS_VERSION, shared by every board. A board
              profile has its own beside it (src/board.h), and its images say
              both: "1.1.0 (S3 1.0.0)". The ESP32's are the core version alone.
              A tag names the core version; a tag with a suffix
              (v1.1.0-dev.8) is published as a pre-release by the workflow.

              A board pre-release (Rob, 2026-09-26, "How a new board comes
              in"): a new board ships first as a pre-release carrying ONLY
              its own image set, so no other board's preview rule can pick it
              up. Its tag is the core version, then the board's key and a
              number: v1.1.2-ws2.1 (BOARD_TAGS below). The core version is
              not bumped for it; the board profile's own version says which
              build it is. A set marked tag_only is built by nothing else.

              A combined preview (Rob, 2026-09-28): several boards' sets
              under one pre-release tag, the core's X.Y.Z then the preview's
              name, v1.1.2-hardware-preview (PREVIEW_TAGS below): the
              Waveshare ESP32-S3-Touch-LCD-4.3B, ESP32-S3-Touch-LCD-2 and
              ESP32-S3-ETH, and the Makerfabs Parallel TFT 3.5" v1.0, and
              nothing else. Since those four merged into 1.2.0, a plain
              v<BBS_VERSION> tag builds them too, with every other set; a
              preview or board tag still builds only its own sets.

Design:       Each release environment (esp32dev_release, ws_s3_lcd147_release,
              freenove_wrover_cam_release, esp32cam_aithinker_release,
              ws_s3touch43b_release, ws_s3touch2_release, ws_s3eth_release,
              makerfabs_s3_par35_release, guition_4848s040_release)
              defines BBS_RELEASE, which makes main.cpp ignore include/secrets.h
              even when it is present. The screens image is built from data/screens only,
              never from data/, because data/system.cfg on a developer's
              machine carries the staff passwords. And then, belt and braces,
              every output file is searched for any password or network name
              this machine knows about, and the release is refused on a
              match. The secret is never printed, only the file it was in.

              The notices are assembled from the licence files of the exact
              packages the build used, not written by hand: a hand-kept list
              is a list somebody forgets to update.

Usage:        python3 tools/release.py                build and check
              python3 tools/release.py --allow-dirty  from a working tree
              python3 tools/release.py --tag v1.0.0   the tag must match
              python3 tools/release.py --tag v1.1.2-hardware-preview
                                                      the preview's four sets
              python3 tools/release.py --tag v1.1.2-ws2.1
                                                      one board's set only
              python3 tools/release.py --board esp32s3-eth
                                                      one set only, locally,
                                                      into a folder of its own

Libraries:    Python 3 standard library; PlatformIO on the PATH
Targets:      developer PC, GitHub Actions (ubuntu-latest)
See also:     .github/workflows/release.yml, README.md "Releases"

Copyright 2026 - Robert Mech
License:      GNU General Public License v3 or later
SPDX-License-Identifier: GPL-3.0-or-later
===========================================================================
"""

import argparse
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
PARTS = ("bootloader.bin", "partitions.bin", "ota_data_initial.bin",
         "firmware.bin", "storage.bin")
NOTICES = "THIRD_PARTY_NOTICES.md"

# The builds a release carries: the directory a family's parts go in (the
# name the directory server keys FLASH_FAMILIES by), the PlatformIO release
# environment, the chipFamily ESP Web Tools matches against the chip it
# reads out of the board, and where that family's bootloader goes. 0x1000
# on the ESP32, 0x0 on the S3 (ESP-IDF's bootloader guide, per target; the
# generated sdkconfig's CONFIG_BOOTLOADER_OFFSET_IN_FLASH is checked
# against it below). The ESP32 is first: its assets keep their plain names.
#
# "table" is the family's partition table: the S3s have their own 8 MB
# layout since 1.1.2 (partitions_s3.csv), so the installer's offsets are
# per family, read from the table, never typed beside it.
#
# "board" is the board profile's define (src/board.h), or None for the
# reference build. A profile has a version of its own beside the core's,
# and a family's version string is what BBS_VERSION_SHOWN makes of the two:
# "1.1.0" for the ESP32, "1.1.0 (S3 1.0.0)" for the Waveshare S3.
BUILDS = (
    {"dir": "esp32",   "env": "esp32dev_release",     "family": "ESP32",    "boot": 0x1000,
     "board": None, "table": "partitions.csv"},
    {"dir": "esp32s3", "env": "ws_s3_lcd147_release", "family": "ESP32-S3", "boot": 0x0,
     "board": "BBS_BOARD_WS_S3LCD147", "table": "partitions_s3.csv"},
    # The Freenove ESP32-WROVER CAM (1.1.0). The same chipFamily as the
    # WROOM's, so ESP Web Tools cannot tell the two apart by reading the chip:
    # the site's picker asks which board. Either image on the other board
    # still boots (internal/PLAN-freenove-cam.md, section 8), but the WROOM's
    # image here drives its SPI card pins (5, 18, 23) against the camera's
    # data lines, so the picker must not guess.
    {"dir": "esp32-fncam", "env": "freenove_wrover_cam_release", "family": "ESP32", "boot": 0x1000,
     "board": "BBS_BOARD_FN_WROVER_CAM", "table": "partitions.csv"},
    # The AI-Thinker ESP32-CAM (1.1.1). chipFamily ESP32 again, so the same
    # rule as the Freenove's: the site's picker asks which board. Its SPI card
    # (CS 13, MOSI 15, CLK 14, MISO 2) and XCLK on GPIO 0 are nothing like the
    # WROOM's or the Freenove's, so no other set is a safe guess for it.
    {"dir": "esp32-cam", "env": "esp32cam_aithinker_release", "family": "ESP32", "boot": 0x1000,
     "board": "BBS_BOARD_AI_ESP32CAM", "table": "partitions.csv"},
    # The Waveshare ESP32-S3-Touch-LCD-4.3B (WS43B, 1.1.2). chipFamily
    # ESP32-S3, the Waveshare stick's, so the site's picker asks which board:
    # the stick's image here would drive its SPI panel and card pins into
    # this board's RGB bus, and this one's would bring an I2C expander up on
    # the stick's panel pins.
    #
    # The four hardware-preview boards were "tag_only" (built by their
    # pre-release tags alone) until the profiles merged into 1.2.0, and the
    # Makerfabs v2.0 until 1.2.1; a set marked so is left out of a plain
    # vX.Y.Z. None is now.
    {"dir": "esp32s3-ws43b", "env": "ws_s3touch43b_release", "family": "ESP32-S3", "boot": 0x0,
     "board": "BBS_BOARD_WS_S3TOUCH43B", "table": "partitions_s3.csv"},
    # The Waveshare ESP32-S3-Touch-LCD-2 (WS2 1.0.0). chipFamily ESP32-S3,
    # the LCD-1.47's, and the same S3R8 and 8 MB layout, so either image boots
    # on the other; but the 1.47's drives its panel on pins that are this
    # board's camera and IMU lines, and this one's the other way round, so
    # the site's picker asks which board.
    {"dir": "esp32s3-ws2", "env": "ws_s3touch2_release", "family": "ESP32-S3", "boot": 0x0,
     "board": "BBS_BOARD_WS_S3TOUCH2", "table": "partitions_s3.csv"},
    # The Waveshare ESP32-S3-ETH (ETH 1.0.0, 1.1.2). chipFamily ESP32-S3,
    # the stick's, so the site's picker asks which board: the stick's image
    # here would drive its panel's pins into this board's camera bus, and
    # this one's would bring the W5500 up on the stick's SD and panel pins.
    {"dir": "esp32s3-eth", "env": "ws_s3eth_release", "family": "ESP32-S3", "boot": 0x0,
     "board": "BBS_BOARD_WS_S3ETH", "table": "partitions_s3.csv"},
    # The Makerfabs ESP32-S3 Parallel TFT 3.5", hardware v1.0 (MF35 1.1.0,
    # SSH on, the S3's 8 MB layout). chipFamily ESP32-S3, the Waveshare's, so
    # the site's picker asks which board: the Waveshare's image here looks for
    # octal PSRAM on a quad part, and this one's looks for quad on the
    # Waveshare's octal. Not for the v2.0 board (octal PSRAM, another bus).
    {"dir": "esp32s3-mf35", "env": "makerfabs_s3_par35_release", "family": "ESP32-S3", "boot": 0x0,
     "board": "BBS_BOARD_MF_S3PAR35", "table": "partitions_s3.csv"},
    # And its hardware v2.0 (MF35V2 1.0.0): octal N16R8, the strobes moved,
    # the console on the chip's own USB. The v1.0's image here looks for quad
    # PSRAM on an octal part and drives WR, D/C and CS on octal PSRAM's pins.
    # tag_only while it was a board lane on 1.2.0; an ordinary set since the
    # profile merged into 1.2.1.
    {"dir": "esp32s3-mf35v2", "env": "makerfabs_s3_par35v2_release", "family": "ESP32-S3", "boot": 0x0,
     "board": "BBS_BOARD_MF_S3PAR35V2", "table": "partitions_s3.csv"},
    # The Guition ESP32-4848S040 (G4848 1.0.0, on 1.2.0), the AITRIP 4.0":
    # chipFamily ESP32-S3, so the site's picker asks which board. Another
    # S3's image here puts its console on USB pins that are this board's
    # touch and panel lines, and this one's would drive an RGB bus into
    # another board's card and panel pins. tag_only while it was a board
    # lane on 1.2.0; an ordinary set since the profile merged into 1.2.1.
    {"dir": "esp32s3-g4848", "env": "guition_4848s040_release", "family": "ESP32-S3", "boot": 0x0,
     "board": "BBS_BOARD_GT_4848S040", "table": "partitions_s3.csv"},
)

# A board pre-release's key, the word in its tag after the core version
# (v1.1.2-ws2.1), and the one set it carries.
BOARD_TAGS = {"ws43b": "esp32s3-ws43b", "ws2": "esp32s3-ws2", "eth": "esp32s3-eth",
              "mf35": "esp32s3-mf35", "mf35v2": "esp32s3-mf35v2", "g4848": "esp32s3-g4848"}

# A combined preview: several boards' sets under one pre-release tag, the
# core version's X.Y.Z then the name (v1.1.2-hardware-preview, Rob,
# 2026-09-28: the three new Waveshare boards and the Makerfabs, shipped
# together). No other set is built, so nothing already released changes.
PREVIEW_TAGS = {
    "hardware-preview": ("esp32s3-ws43b", "esp32s3-ws2", "esp32s3-eth", "esp32s3-mf35"),
}


def select(ver, tag, board):
    """Which sets a run builds, and the folder it writes (release/<relname>/).

    No tag, or v<BBS_VERSION>: every set that is not tag_only, into
    release/<BBS_VERSION>/. v<X.Y.Z>-<preview>: that preview's sets, into
    release/<the tag less its v>/, the folder the workflow publishes from.
    v<BBS_VERSION or X.Y.Z>-<board>.<n>: that board's set alone, likewise.
    --board DIR (no tag): one set, into release/<BBS_VERSION>-DIR/, so a
    local build of one board never writes over a full release's folder.
    """
    dirs = {b["dir"] for b in BUILDS}
    for name, want in list(BOARD_TAGS.items()) + [(n, d) for n, ds in PREVIEW_TAGS.items() for d in ds]:
        if want not in dirs:
            die(f"release.py: {name} names a set, {want}, that BUILDS does not have")
    core = ver.split("-")[0]                  # 1.1.2-hw.1 -> 1.1.2
    if board:
        if tag:
            die("--board builds one set locally; a tag says its own sets, so give one or the other")
        builds = tuple(b for b in BUILDS if b["dir"] == board)
        if not builds:
            die(f"no set called {board}; the sets are {', '.join(b['dir'] for b in BUILDS)}")
        return builds, f"{ver}-{board}"
    if not tag or tag == f"v{ver}":
        return tuple(b for b in BUILDS if not b.get("tag_only")), ver
    m = re.match(r"^v" + re.escape(core) + r"-([a-z][a-z0-9-]*[a-z0-9])$", tag)
    if m and m.group(1) in PREVIEW_TAGS:
        want = PREVIEW_TAGS[m.group(1)]
        builds = tuple(b for b in BUILDS if b["dir"] in want)
        print(f"release: the {m.group(1)} pre-release, {tag[1:]}: {', '.join(b['dir'] for b in builds)}")
        return builds, tag[1:]
    m = re.match(r"^v(?:" + re.escape(ver) + "|" + re.escape(core) + r")-([a-z0-9]+)\.(\d{1,3})$", tag)
    if m and m.group(1) in BOARD_TAGS:
        builds = tuple(b for b in BUILDS if b["dir"] == BOARD_TAGS[m.group(1)])
        print(f"release: a board pre-release, {tag[1:]}: the {builds[0]['dir']} set only")
        return builds, tag[1:]
    die(f"tag {tag} does not match BBS_VERSION {ver}, nor v{core}-<preview> for a preview in "
        f"{sorted(PREVIEW_TAGS)}, nor v{core}-<board>.<n> for a board in {sorted(BOARD_TAGS)}")

# Offsets every table keeps, because ESP-IDF and PlatformIO put them there
# for any table (otadata after nvs, the first app at 0x20000). Everything
# else, storage's offset and the app slot's size, comes from each family's
# own table (1.1.2): typed beside the table, a count goes stale.
FIXED = {"otadata": 0xF000, "ota_0": 0x20000}


def die(msg):
    print(f"release: {msg}", file=sys.stderr)
    sys.exit(1)


def write(path, text, enc="utf-8"):
    """Text with LF line ends on every OS (write_text grew newline= in 3.10)."""
    with open(path, "w", encoding=enc, newline="\n") as f:
        f.write(text)


def version():
    src = (ROOT / "src" / "config.h").read_text(encoding="utf-8")
    m = re.search(r'#define\s+BBS_VERSION\s+"([^"]+)"', src)
    if not m:
        die("no BBS_VERSION in src/config.h")
    return m.group(1)


def shown_version(core, board):
    """The version a family's images say they are, as BBS_VERSION_SHOWN in
    src/config.h builds it: the core version, then the board profile's tag
    and version in brackets when there is a profile. Read out of the
    profile's own block in src/board.h, so there is one source for it."""
    if not board:
        return core
    text = (ROOT / "src" / "board.h").read_text(encoding="utf-8")
    m = re.search(r"^#if defined\(" + re.escape(board) + r"\)$(.*?)^#endif\s*//\s*" + re.escape(board),
                  text, re.S | re.M)
    if not m:
        die(f"src/board.h has no block for {board}")
    tag = re.search(r'#define\s+BBS_BOARD_TAG\s+"([^"]+)"', m.group(1))
    bver = re.search(r'#define\s+BBS_BOARD_VERSION\s+"([^"]+)"', m.group(1))
    if not tag or not bver:
        die(f"src/board.h: {board} has no BBS_BOARD_TAG and BBS_BOARD_VERSION")
    return f"{core} ({tag.group(1)} {bver.group(1)})"


def git(*args):
    return subprocess.run(["git", *args], cwd=ROOT, capture_output=True,
                          text=True).stdout.strip()


def pio(*args, env=None):
    exe = shutil.which("pio") or shutil.which("platformio")
    cmd = [exe] if exe else [sys.executable, "-m", "platformio"]
    r = subprocess.run(cmd + list(args), cwd=ROOT, env=env)
    if r.returncode:
        die(f"pio {' '.join(args)} failed")


def check_partitions(name="partitions.csv"):
    """One family's table: {label: (offset, size)}, checked for what the
    installer relies on. storage must be the last filesystem partition
    (PlatformIO's uploadfs writes the last one, and the accounts must never
    be in its way)."""
    table = {}
    order = []
    for line in (ROOT / name).read_text().splitlines():
        line = line.split("#", 1)[0].strip()
        if not line:
            continue
        f = [x.strip() for x in line.split(",")]
        if len(f) >= 5:
            table[f[0]] = (int(f[3], 0), int(f[4], 0))
            order.append((f[0], f[1], f[2]))
    for label, off in FIXED.items():
        if label not in table or table[label][0] != off:
            die(f"{name}: {label} is not at 0x{off:X}; the installer writes it there")
    if "storage" not in table:
        die(f"{name}: no storage partition")
    fs = [lab for lab, typ, sub in order if typ == "data" and sub in ("spiffs", "fat", "littlefs")]
    if not fs or fs[-1] != "storage":
        die(f"{name}: storage must be the last filesystem partition (uploadfs writes the last one)")
    return table


def table_bin(data):
    """The labels and offsets in a built partitions.bin (32-byte entries,
    magic 0xAA50, label at 12): what the board will really boot with."""
    out = {}
    for i in range(0, len(data) - 31, 32):
        e = data[i:i + 32]
        if e[0:2] != b"\xaa\x50":
            break
        out[e[12:28].split(b"\0", 1)[0].decode("ascii", "replace")] = (
            int.from_bytes(e[4:8], "little"), int.from_bytes(e[8:12], "little"))
    return out


def secrets_known():
    """Every password or network name this machine could leak. Values only."""
    found = []
    sh = ROOT / "include" / "secrets.h"
    if sh.exists():
        for m in re.finditer(r'#define\s+WIFI_(?:SSID|PASS)\s+"([^"]*)"', sh.read_text()):
            found.append(m.group(1))
    cfg = ROOT / "data" / "system.cfg"
    if cfg.exists():
        for line in cfg.read_text(errors="replace").splitlines():
            m = re.match(r"\s*(\w*password\w*|wifi_ssid|token)\s*=\s*(.+?)\s*$", line)
            if m and m.group(2) != "***":
                found.append(m.group(2))
    return [s for s in found if len(s) >= 4]


def framework_dir():
    """The ESP-IDF package the build used, found by its version."""
    base = Path.home() / ".platformio" / "packages"
    for d in sorted(base.glob("framework-espidf*")):
        pj = d / "package.json"
        if pj.exists() and json.loads(pj.read_text()).get("version") == "3.50301.0":
            return d
    die("framework-espidf 3.50301.0 (ESP-IDF 5.3.1) not found under ~/.platformio/packages")


def notices(fw):
    """THIRD_PARTY_NOTICES.md from the licence files of what is linked in."""
    mc = ROOT / "managed_components"
    items = [
        ("ESP-IDF 5.3.1 (Espressif)", "Apache-2.0", fw / "LICENSE"),
        ("FreeRTOS kernel", "MIT", fw / "components/freertos/FreeRTOS-Kernel/LICENSE.md"),
        ("lwIP TCP/IP stack", "BSD-3-Clause", fw / "components/lwip/lwip/COPYING"),
        ("Mbed TLS", "Apache-2.0", fw / "components/mbedtls/mbedtls/LICENSE"),
        ("FatFs (ChaN)", "FatFs licence (BSD-style)", fw / "components/fatfs/src/ff.c"),
        ("Espressif Wi-Fi libraries", "see text", fw / "components/esp_wifi/lib/LICENSE"),
        ("Espressif PHY libraries", "see text", fw / "components/esp_phy/lib/LICENSE"),
        ("newlib C library", "see text", fw / "components/newlib/COPYING.NEWLIB"),
        ("esp_littlefs (joltwallet)", "MIT", mc / "joltwallet__littlefs/LICENSE"),
        ("littlefs", "BSD-3-Clause", mc / "joltwallet__littlefs/src/littlefs/LICENSE.md"),
        ("mDNS (Espressif)", "Apache-2.0", mc / "espressif__mdns/LICENSE"),
        # In the ESP32-S3 image (the panel) and the camera image (the
        # watermark): the bitmap font.
        ("Spleen bitmap font 2.2.0 (Frederic Cambus), ESP32-S3 and camera images", "BSD-2-Clause",
         ROOT / "tools/fonts/SPLEEN-LICENSE"),
        # In the camera images only (the Freenove and the ESP32-CAM): the
        # camera driver and its JPEG encoder, and the decoder the watermark
        # uses, which is in the chip's ROM (its notice is the header of the
        # same code in esp_jpeg).
        ("esp32-camera 2.1.7 (Espressif), camera images", "Apache-2.0",
         mc / "espressif__esp32-camera/LICENSE"),
        ("TJpgDec (ChaN), in the chip's ROM, camera images", "TJpgDec licence (BSD-style)",
         mc / "espressif__esp_jpeg/tjpgd/tjpgd.c"),
        # In the ESP32-S3 image only (1.1.2): SSH. GPLv3 or later in every
        # file header; the GPLv3 text is the project's own LICENSE.
        ("wolfSSL / wolfCrypt 5.9.4 (wolfSSL Inc.), ESP32-S3 image", "GPL-3.0-or-later",
         ROOT / "components/wolfssh/wolfssl/LICENSING"),
        ("wolfSSH 1.5.0 (wolfSSL Inc.), ESP32-S3 image", "GPL-3.0-or-later",
         ROOT / "components/wolfssh/wolfssh/LICENSING"),
    ]
    out = ["# Third-party notices",
           "",
           "The µnleashed BBS firmware is free software under the GNU General Public",
           "License, version 3 or later. The release images also contain the",
           "following components, each under its own licence, reproduced below from",
           "the exact packages this release was built with.",
           ""]
    for name, lic, _ in items:
        out.append(f"- {name}: {lic}")
    for name, lic, path in items:
        if not path.exists():
            die(f"licence file missing: {path}")
        text = path.read_text(encoding="utf-8", errors="replace")
        if path.name in ("ff.c", "tjpgd.c"):           # the notice is the file header
            text = text.split("*/", 1)[0] if "*/" in text else "\n".join(text.splitlines()[:25])
        out += ["", "---", "", f"## {name}", "", "```", text.rstrip(), "```"]
    return "\n".join(out) + "\n"


# A copyright, licence or author line that names anybody but the project's
# author is a release blocker when the name is an AI tool or its maker. Rob,
# 2026-09-23: the copyright is his, and that must never change by accident.
NOTICE_LINE = re.compile(
    r"^\W*(copyright|\(c\)|©|spdx-filecopyrighttext|spdx-license-identifier|"
    r"licen[cs]ed?\s+(to|by)|authors?\s*:|maintainers?\s*:|co-authored-by\s*:|"
    r"generated\s+(with|by))[^\n]*\b(anthropic|claude)\b",
    re.I | re.M)

# The project is GPL-3.0-or-later from 1.1.0-dev.11 (Rob, 2026-09-24). A
# file that still says GPL-2.0 in its SPDX line was added on an old header
# and would ship a licence the project no longer grants. Anchored at the
# start of a line so prose and this pattern itself do not match.
OLD_SPDX = re.compile(r"^\W*SPDX-License-Identifier:[^\n]*\bGPL-2\.0", re.M)


def check_notices():
    """Refuse to release if any tracked text file carries a copyright,
    licence or author line naming Anthropic or Claude, or a GPL-2.0 SPDX
    line."""
    old = []
    for rel in git("ls-files").splitlines():
        try:
            text = (ROOT / rel).read_text(encoding="utf-8")
        except (OSError, UnicodeDecodeError):
            continue
        for m in OLD_SPDX.finditer(text):
            old.append(f"{rel}:{text.count(chr(10), 0, m.start()) + 1}")
    if old:
        die("GPL-2.0 SPDX lines; the project is GPL-3.0-or-later: " + ", ".join(old[:10]))
    bad = []
    for rel in git("ls-files").splitlines():
        p = ROOT / rel
        try:
            text = p.read_text(encoding="utf-8")
        except (OSError, UnicodeDecodeError):
            continue                      # binary: screens, images
        for m in NOTICE_LINE.finditer(text):
            line = text.count("\n", 0, m.start()) + 1
            bad.append(f"{rel}:{line}")
    if bad:
        die("copyright or licence lines name Anthropic or Claude: " + ", ".join(bad[:10]))


def check_formats():
    """Every board's printf is newlib nano (1.1.2), which has no %ll, %z,
    %hh, %j, %t, positional arguments or floats; tools/check_formats.py
    refuses them in src/, after proving on its own cases that it can see
    them."""
    tool = str(ROOT / "tools" / "check_formats.py")
    for args in (["--self-test"], [str(ROOT / "src")]):
        r = subprocess.run([sys.executable, tool, *args], cwd=ROOT,
                           capture_output=True, text=True)
        if r.returncode:
            die("printf formats newlib nano cannot print:\n" + r.stdout.strip())


def check_nano(b):
    """The generated sdkconfig is the only place a Kconfig choice is proven
    to have taken: a value a layer cannot set is dropped without a word."""
    cfg = ROOT / f"sdkconfig.{b['env']}"
    if "CONFIG_NEWLIB_NANO_FORMAT=y" not in cfg.read_text(encoding="utf-8"):
        die(f"{cfg.name} does not say CONFIG_NEWLIB_NANO_FORMAT=y; delete it and build again")


def ext_plugins(b):
    """The plugins from their own repositories this build carries (1.2.0,
    LINK.md): each fetched at its locked commit with tools/plugins.py
    --release, which refuses a local path, a working tree, a checkout that
    is not the locked commit, a licence the firmware cannot carry, and the
    notice lines check_notices() refuses. Returns [(name, version, commit)]
    for release.txt."""
    tool = ROOT / "tools" / "plugins.py"
    r = subprocess.run([sys.executable, str(tool), "env", b["env"]], cwd=ROOT,
                       capture_output=True, text=True)
    if r.returncode:
        die(f"{b['env']}: could not read custom_ext_plugins: {r.stderr.strip()}")
    names = r.stdout.split()
    if not names:
        return []
    r = subprocess.run([sys.executable, str(tool), "fetch", "--release", *names], cwd=ROOT)
    if r.returncode:
        die(f"{b['env']}: its plugins did not pass tools/plugins.py --release")
    # newlib nano's formats (1.1.2) in each plugin's board sources too:
    # check_formats() above saw src/ only, before these were fetched.
    tool = str(ROOT / "tools" / "check_formats.py")
    for n in names:
        r = subprocess.run([sys.executable, tool, str(ROOT / "ext" / n / "bbs")], cwd=ROOT,
                           capture_output=True, text=True)
        if r.returncode:
            die(f"{b['env']}: plugin {n} has printf formats newlib nano cannot print:\n" + r.stdout.strip())
    out = []
    for n in names:
        ini = (ROOT / "ext" / n / "unleashed-plugin.ini").read_text(encoding="utf-8")
        m = re.search(r"^version\s*=\s*(\S+)", ini, re.M)
        commit = git("-C", str(ROOT / "ext" / n), "rev-parse", "HEAD")
        out.append((n, m.group(1) if m else "?", commit))
    return out


def check_boot_offset(b):
    """The bootloader offset BUILDS names for a family is the one its build
    was made for: CONFIG_BOOTLOADER_OFFSET_IN_FLASH in the generated
    sdkconfig. A wrong offset here writes a bootloader where the chip will
    never look for it, and the board simply never boots."""
    cfg = ROOT / f"sdkconfig.{b['env']}"
    if not cfg.exists():
        die(f"no {cfg.name} after building {b['env']}")
    m = re.search(r"^CONFIG_BOOTLOADER_OFFSET_IN_FLASH=(0x[0-9a-fA-F]+)$", cfg.read_text(), re.M)
    if not m or int(m.group(1), 16) != b["boot"]:
        die(f"{b['env']}: the build puts its bootloader at {m.group(1) if m else '?'}, "
            f"not 0x{b['boot']:X} as BUILDS says")


def manifest(b):
    """An ESP Web Tools manifest for one family, in that family's folder,
    part paths relative to it. The directory server makes its own from what
    it finds on disk; this one is for trying a release by hand. One family a
    manifest, because a manifest has one version and the families' differ:
    ESP Web Tools compares it with what Improv reports ("unleashed BBS",
    then the version as the board shows it) to decide whether to offer
    Update."""
    t = b["parts"]
    offsets = {"partitions.bin": 0x8000, "ota_data_initial.bin": t["otadata"][0],
               "firmware.bin": t["ota_0"][0], "storage.bin": t["storage"][0]}
    m = {"name": "unleashed BBS", "version": b["version"], "new_install_prompt_erase": True,
         "builds": [{"chipFamily": b["family"],
                     "parts": [{"path": name,
                                "offset": b["boot"] if name == "bootloader.bin" else offsets[name]}
                               for name in PARTS]}]}
    return json.dumps(m, indent=2) + "\n"


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("Purpose:")[0])
    ap.add_argument("--allow-dirty", action="store_true",
                    help="build from a working tree with uncommitted changes")
    ap.add_argument("--tag", help="the git tag being released: v<BBS_VERSION>, or "
                                  "v<BBS_VERSION>-<board>.<n> for one board's pre-release")
    ap.add_argument("--board", help="build only this set (a BUILDS dir, such as esp32s3-eth): "
                                    "how a tag_only board is built, locally or for a board pre-release")
    a = ap.parse_args()

    ver = version()
    builds, relname = select(ver, a.tag, a.board)
    dirty = git("status", "--porcelain", "--untracked-files=no")
    if dirty and not a.allow_dirty:
        die("the working tree has uncommitted changes; commit, or --allow-dirty to test")
    for b in builds:
        b["parts"] = check_partitions(b["table"])
    check_notices()
    check_formats()

    # The screens image, from data/screens only.
    stage = ROOT / ".pio" / "release-data"
    if stage.exists():
        shutil.rmtree(stage)
    shutil.copytree(ROOT / "data" / "screens", stage / "screens")

    # The same PLATFORMIO_DATA_DIR for both runs. PlatformIO treats a change
    # of data directory as a changed project and cleans the build directory,
    # so setting it for buildfs alone deleted the firmware images the first
    # run had just made.
    env = dict(os.environ, PLATFORMIO_DATA_DIR=str(stage))

    # Every family's five parts, built and checked before anything is
    # written: a release is all of its families or none of them.
    families = []
    for b in builds:
        b["plugins"] = ext_plugins(b)
        pio("run", "-e", b["env"], env=env)
        pio("run", "-e", b["env"], "-t", "buildfs", env=env)
        build = ROOT / ".pio" / "build" / b["env"]
        src = {"bootloader.bin": build / "bootloader.bin",
               "partitions.bin": build / "partitions.bin",
               "ota_data_initial.bin": build / "ota_data_initial.bin",
               "firmware.bin": build / "firmware.bin",
               "storage.bin": build / "littlefs.bin"}
        for name, p in src.items():
            if not p.exists():
                die(f"{b['env']} did not produce {p}")
        if src["firmware.bin"].stat().st_size > b["parts"]["ota_0"][1]:
            die(f"{b['env']}: firmware.bin does not fit the OTA slot")
        built = table_bin(src["partitions.bin"].read_bytes())
        for label in ("otadata", "ota_0", "storage"):
            if built.get(label) != b["parts"][label]:
                die(f"{b['env']}: the built partitions.bin puts {label} at {built.get(label)}, "
                    f"not {b['parts'][label]} as {b['table']} says")
        check_boot_offset(b)
        check_nano(b)
        b["version"] = shown_version(ver, b["board"])
        blobs = {name: p.read_bytes() for name, p in src.items()}
        # The image says it is the version it is published as: the string
        # the board shows (SYS, ABOUT, Improv) is in the firmware verbatim.
        if b["version"].encode("ascii") not in blobs["firmware.bin"]:
            die(f"{b['env']}: firmware.bin does not carry the version {b['version']!r}")
        families.append((b, blobs))

    for b, blobs in families:
        for secret in secrets_known():
            needle = secret.encode("utf-8", errors="replace")
            for name, data in blobs.items():
                if needle in data:
                    die(f"{b['dir']}/{name} contains a password or network name from this "
                        "machine; refusing to release")
        for name, data in blobs.items():
            if re.search(rb"(?i)anthropic|claude", data):
                die(f"{b['dir']}/{name} mentions Anthropic or Claude; the images carry no such credit")
        if b"sysop_password" in blobs["storage.bin"]:
            die(f"{b['dir']}/storage.bin carries a system.cfg; the screens image must be screens only")

    out = ROOT / "release" / relname
    if out.exists():
        shutil.rmtree(out)
    assets = out / "assets"
    install = out / "install"
    assets.mkdir(parents=True)

    note = notices(framework_dir())
    sums = []
    for b, blobs in families:
        (install / b["dir"]).mkdir(parents=True)
        # The first family's assets keep their plain names, as the
        # directory's fetcher has always read them; the rest are prefixed.
        prefix = "" if b is BUILDS[0] else b["dir"] + "-"
        for name, data in blobs.items():
            (assets / (prefix + name)).write_bytes(data)
            (install / b["dir"] / name).write_bytes(data)
            sums.append(f"{hashlib.sha256(data).hexdigest()}  {prefix + name}")
        # version.txt: one ASCII line, the family's version exactly as the
        # board shows it. The directory's fetcher reads it; the ESP32's is
        # the bare core version.
        vtxt = (b["version"] + "\n").encode("ascii")
        (assets / (prefix + "version.txt")).write_bytes(vtxt)
        (install / b["dir"] / "version.txt").write_bytes(vtxt)
        sums.append(f"{hashlib.sha256(vtxt).hexdigest()}  {prefix}version.txt")
        write(install / b["dir"] / "manifest.json", manifest(b))
    write(assets / NOTICES, note)
    write(install / NOTICES, note)
    sums.append(f"{hashlib.sha256(note.encode('utf-8')).hexdigest()}  {NOTICES}")
    write(assets / "SHA256SUMS", "\n".join(sums) + "\n", "ascii")
    commit = git("rev-parse", "--short", "HEAD") + ("-dirty" if dirty else "")
    # Every plugin built in from its own repository, per family, with the
    # commit it was built from (1.2.0): what went into an image is on record.
    rel = [f"version {relname}", f"commit {commit}"]
    for b, _ in families:
        for name, pver, pcommit in b.get("plugins", []):
            rel.append(f"plugin {b['dir']} {name} {pver} {pcommit}")
    write(install / "release.txt", "\n".join(rel) + "\n", "ascii")

    print(f"release {relname} ({commit})")
    for line in sums:
        print("  " + line)
    print(f"assets:  {assets}")
    print(f"install: {install}  (copy to firmware/{relname}/ on the directory server)")


if __name__ == "__main__":
    main()
