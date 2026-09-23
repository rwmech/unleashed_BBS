#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""
===========================================================================
 µnleashed BBS
 Electronic freedom on a microcontroller.
===========================================================================

File:         tools/release.py
Module:       Tools / release build

Purpose:      Builds a public release: the five flash images the web
              installer writes, the third-party licence notices, and a
              SHA256SUMS over all of it. Run by the GitHub Action on a
              version tag, and by hand to test a release before tagging.

Output:       release/<version>/assets/    flat, for a GitHub Release, the
                                           shape deploy/fetch_release.py in
                                           the directory repository fetches
              release/<version>/install/   the directory server's own layout,
                                           esp32/<parts>, for copying straight
                                           into firmware/<version>/ by hand

Design:       The release environment (esp32dev_release) defines BBS_RELEASE,
              which makes main.cpp ignore include/secrets.h even when it is
              present. The screens image is built from data/screens only,
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

Libraries:    Python 3 standard library; PlatformIO on the PATH
Targets:      developer PC, GitHub Actions (ubuntu-latest)
See also:     .github/workflows/release.yml, README.md "Releases"

Copyright 2026 - Robert Mech
License:      GNU General Public License v2 or later
SPDX-License-Identifier: GPL-2.0-or-later
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
ENV = "esp32dev_release"
BUILD = ROOT / ".pio" / "build" / ENV
PARTS = ("bootloader.bin", "partitions.bin", "ota_data_initial.bin",
         "firmware.bin", "storage.bin")
NOTICES = "THIRD_PARTY_NOTICES.md"

# Offsets the installer writes to, from partitions.csv. Checked here against
# the table itself, so a partition move cannot ship with stale offsets.
EXPECT = {"otadata": 0xF000, "ota_0": 0x20000, "storage": 0x3C0000}
SLOT_MAX = 0x180000          # ota_0 size: the largest firmware.bin that fits


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


def git(*args):
    return subprocess.run(["git", *args], cwd=ROOT, capture_output=True,
                          text=True).stdout.strip()


def pio(*args, env=None):
    exe = shutil.which("pio") or shutil.which("platformio")
    cmd = [exe] if exe else [sys.executable, "-m", "platformio"]
    r = subprocess.run(cmd + list(args), cwd=ROOT, env=env)
    if r.returncode:
        die(f"pio {' '.join(args)} failed")


def check_partitions():
    table = {}
    for line in (ROOT / "partitions.csv").read_text().splitlines():
        line = line.split("#", 1)[0].strip()
        if not line:
            continue
        f = [x.strip() for x in line.split(",")]
        if len(f) >= 5:
            table[f[0]] = (int(f[3], 0), int(f[4], 0))
    for name, off in EXPECT.items():
        if name not in table or table[name][0] != off:
            die(f"partitions.csv: {name} is not at 0x{off:X}; the installer's offsets "
                "(directory server, firmware/README.md) must move with it")
    if table["ota_0"][1] != SLOT_MAX:
        die("partitions.csv: ota_0 changed size; update SLOT_MAX")
    return table


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
    ]
    out = ["# Third-party notices",
           "",
           "The µnleashed BBS firmware is free software under the GNU General Public",
           "License, version 2 or later. The release images also contain the",
           "following components, each under its own licence, reproduced below from",
           "the exact packages this release was built with.",
           ""]
    for name, lic, _ in items:
        out.append(f"- {name}: {lic}")
    for name, lic, path in items:
        if not path.exists():
            die(f"licence file missing: {path}")
        text = path.read_text(encoding="utf-8", errors="replace")
        if path.name == "ff.c":                       # the notice is the file header
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


def check_notices():
    """Refuse to release if any tracked text file carries a copyright,
    licence or author line naming Anthropic or Claude."""
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


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("Purpose:")[0])
    ap.add_argument("--allow-dirty", action="store_true",
                    help="build from a working tree with uncommitted changes")
    ap.add_argument("--tag", help="the git tag being released; must be v<BBS_VERSION>")
    a = ap.parse_args()

    ver = version()
    if a.tag and a.tag != f"v{ver}":
        die(f"tag {a.tag} does not match BBS_VERSION {ver}")
    dirty = git("status", "--porcelain", "--untracked-files=no")
    if dirty and not a.allow_dirty:
        die("the working tree has uncommitted changes; commit, or --allow-dirty to test")
    check_partitions()
    check_notices()

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
    pio("run", "-e", ENV, env=env)
    pio("run", "-e", ENV, "-t", "buildfs", env=env)

    src = {"bootloader.bin": BUILD / "bootloader.bin",
           "partitions.bin": BUILD / "partitions.bin",
           "ota_data_initial.bin": BUILD / "ota_data_initial.bin",
           "firmware.bin": BUILD / "firmware.bin",
           "storage.bin": BUILD / "littlefs.bin"}
    for name, p in src.items():
        if not p.exists():
            die(f"build did not produce {p}")
    if src["firmware.bin"].stat().st_size > SLOT_MAX:
        die("firmware.bin does not fit the OTA slot")

    blobs = {name: p.read_bytes() for name, p in src.items()}
    for secret in secrets_known():
        needle = secret.encode("utf-8", errors="replace")
        for name, data in blobs.items():
            if needle in data:
                die(f"{name} contains a password or network name from this machine; "
                    "refusing to release")
    for name, data in blobs.items():
        if re.search(rb"(?i)anthropic|claude", data):
            die(f"{name} mentions Anthropic or Claude; the images carry no such credit")
    if b"sysop_password" in blobs["storage.bin"]:
        die("storage.bin carries a system.cfg; the screens image must be screens only")

    out = ROOT / "release" / ver
    if out.exists():
        shutil.rmtree(out)
    assets = out / "assets"
    install = out / "install"
    (install / "esp32").mkdir(parents=True)
    assets.mkdir(parents=True)

    note = notices(framework_dir())
    sums = []
    for name, data in blobs.items():
        (assets / name).write_bytes(data)
        (install / "esp32" / name).write_bytes(data)
        sums.append(f"{hashlib.sha256(data).hexdigest()}  {name}")
    write(assets / NOTICES, note)
    write(install / NOTICES, note)
    sums.append(f"{hashlib.sha256(note.encode('utf-8')).hexdigest()}  {NOTICES}")
    write(assets / "SHA256SUMS", "\n".join(sums) + "\n", "ascii")
    commit = git("rev-parse", "--short", "HEAD") + ("-dirty" if dirty else "")
    write(install / "release.txt", f"version {ver}\ncommit {commit}\n", "ascii")

    print(f"release {ver} ({commit})")
    for line in sums:
        print("  " + line)
    print(f"assets:  {assets}")
    print(f"install: {install}  (copy to firmware/{ver}/ on the directory server)")


if __name__ == "__main__":
    main()
