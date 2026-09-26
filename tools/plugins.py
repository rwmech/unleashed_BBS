#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""
===========================================================================
 µnleashed BBS
 Electronic freedom on a microcontroller.
===========================================================================

File:         tools/plugins.py
Module:       Tools / plugins kept in their own repositories (1.2.0)

Purpose:      Fetches the plugins a build names, each at exactly the commit
              plugins.lock pins, into ext/<name>/, and checks each one before
              anything is compiled: its manifest, the plugin API it needs
              against this core's, and the licence line on every source it
              puts into the firmware. LINK.md, "Plugins in their own
              repositories", is the design; this is its tool.

              plugins.lock, one plugin a line:

                  # name   source                                      commit
                  camsat   https://github.com/rwmech/unleashed_camsat  3f2a9c1e...(40 hex)
                  hello    ../unleashed_hello                          -

              source is a git URL or a local path (relative to the repo).
              commit is the full 40-character commit, or "-" for a local
              path's working tree as it is (development only). A release
              takes neither a local path nor "-".

Usage:        python3 tools/plugins.py fetch [NAME ...] [--lock FILE] [--release]
              python3 tools/plugins.py check [NAME ...] [--lock FILE] [--release]
              python3 tools/plugins.py list  [--lock FILE]
              python3 tools/plugins.py env ENV       the env's custom_ext_plugins

Libraries:    Python 3 standard library; git on the PATH
Targets:      developer PC, GitHub Actions
See also:     LINK.md, tools/pio_plugins.py, tools/release.py, PLUGINS.md

Copyright 2026 - Robert Mech
License:      GNU General Public License v3 or later
SPDX-License-Identifier: GPL-3.0-or-later
===========================================================================
"""

import argparse
import configparser
import pathlib
import re
import shutil
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
EXT = ROOT / "ext"
LOCK = ROOT / "plugins.lock"
NAME = re.compile(r"^[a-z0-9_]{1,15}$")
COMMIT = re.compile(r"^[0-9a-f]{40}$")
SYMBOL = re.compile(r"^[A-Za-z_][A-Za-z0-9_]*$")

# Licences whose code may be combined into the GPL-3.0-or-later firmware.
COMPATIBLE = {
    "GPL-3.0-or-later", "GPL-3.0-only", "GPL-3.0", "LGPL-3.0-or-later", "LGPL-3.0-only",
    "LGPL-2.1-or-later", "GPL-2.0-or-later", "Apache-2.0", "MIT", "BSD-2-Clause", "BSD-3-Clause",
    "ISC", "Zlib", "0BSD", "Unlicense", "CC0-1.0",
}
SPDX = re.compile(r"SPDX-License-Identifier:\s*([^\s*/]+)")
# The same line release.py refuses in the core: a copyright, licence or
# author line naming Anthropic or Claude.
NOTICE = re.compile(
    r"^\W*(copyright|\(c\)|©|spdx-filecopyrighttext|spdx-license-identifier|"
    r"licen[cs]ed?\s+(to|by)|authors?\s*:|maintainers?\s*:|co-authored-by\s*:|"
    r"generated\s+(with|by))[^\n]*\b(anthropic|claude)\b", re.I | re.M)


def die(msg):
    print(f"plugins: {msg}", file=sys.stderr)
    sys.exit(1)


def git(*args, cwd=None):
    r = subprocess.run(["git", *args], cwd=cwd, capture_output=True, text=True)
    if r.returncode:
        die(f"git {' '.join(args)}: {r.stderr.strip() or r.stdout.strip()}")
    return r.stdout.strip()


def read_lock(path):
    """{name: (source, commit)} from a lock file."""
    out = {}
    if not path.exists():
        return out
    for n, raw in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        line = raw.split("#", 1)[0].strip()
        if not line:
            continue
        parts = line.split()
        if len(parts) != 3:
            die(f"{path.name}:{n}: want 'name source commit'")
        name, src, commit = parts
        if not NAME.match(name):
            die(f"{path.name}:{n}: '{name}' is not a plugin name (a-z 0-9 _, 15 at most)")
        if commit != "-" and not COMMIT.match(commit):
            die(f"{path.name}:{n}: the commit must be the full 40 hex characters, or -")
        out[name] = (src, commit)
    return out


def local(src):
    return not re.match(r"^[a-z][a-z0-9+.-]*://", src) and not src.startswith("git@")


def core_api():
    text = (ROOT / "src" / "core" / "plugin.h").read_text(encoding="utf-8")
    ma = re.search(r"#define BBS_PLUGIN_API_MAJOR (\d+)", text)
    mi = re.search(r"#define BBS_PLUGIN_API_MINOR (\d+)", text)
    if not ma or not mi:
        die("src/core/plugin.h has no BBS_PLUGIN_API_MAJOR/MINOR")
    return int(ma.group(1)), int(mi.group(1))


def manifest(name):
    ini = EXT / name / "unleashed-plugin.ini"
    if not ini.exists():
        die(f"ext/{name} has no unleashed-plugin.ini")
    cp = configparser.ConfigParser(inline_comment_prefixes=(";", "#"))
    cp.read_string("[m]\n" + ini.read_text(encoding="utf-8"))
    return dict(cp["m"])


def fetch_one(name, src, commit, release):
    if release and (local(src) or commit == "-"):
        die(f"{name}: a release takes a pinned commit from a repository, not a local path or '-'")
    dest = EXT / name
    EXT.mkdir(exist_ok=True)
    source = (ROOT / src).resolve() if local(src) else src
    if commit == "-":
        # A local working tree as it is, for developing a plugin beside the core.
        if not local(src) or not pathlib.Path(source).is_dir():
            die(f"{name}: '-' needs a local folder, and {src} is not one")
        if dest.exists():
            shutil.rmtree(dest)
        shutil.copytree(source, dest, ignore=shutil.ignore_patterns(".git"))
        return "working tree"
    if not (dest / ".git").exists():
        if dest.exists():
            shutil.rmtree(dest)
        git("clone", "--quiet", "--no-checkout", str(source), str(dest))
    have = subprocess.run(["git", "cat-file", "-e", commit + "^{commit}"], cwd=dest,
                          capture_output=True).returncode == 0
    if not have:
        git("fetch", "--quiet", "origin", cwd=dest)
    git("checkout", "--quiet", "--detach", "--force", commit, cwd=dest)
    git("clean", "-fdxq", cwd=dest)
    head = git("rev-parse", "HEAD", cwd=dest)
    if head != commit:
        die(f"{name}: ext/{name} is at {head}, not the locked {commit}")
    return commit[:12]


def check_one(name, release):
    m = manifest(name)
    if m.get("name") != name:
        die(f"ext/{name}: its manifest calls it '{m.get('name')}'")
    if not SYMBOL.match(m.get("descriptor", "")):
        die(f"{name}: the manifest names no descriptor")
    want = m.get("api", "")
    mt = re.match(r"^(\d+)\.(\d+)$", want)
    if not mt:
        die(f"{name}: api must be MAJOR.MINOR, not '{want}'")
    major, minor = core_api()
    if int(mt.group(1)) != major:
        die(f"{name}: written for plugin API {want}; this core's is {major}.{minor}")
    if int(mt.group(2)) > minor:
        die(f"{name}: needs plugin API {want}; this core has {major}.{minor}: update the core")
    lic = m.get("license", "")
    if lic not in COMPATIBLE:
        die(f"{name}: licence '{lic}' cannot go into the GPL-3.0-or-later firmware")
    srcs = sorted(p for p in (EXT / name / "bbs").rglob("*") if p.suffix in (".c", ".cpp", ".h", ".hpp"))
    if not srcs:
        die(f"{name}: nothing in its bbs/ folder")
    for p in srcs:
        text = p.read_text(encoding="utf-8", errors="replace")
        rel = p.relative_to(ROOT)
        ids = SPDX.findall(text)
        if not ids:
            die(f"{rel}: no SPDX-License-Identifier line")
        for i in ids:
            if i not in COMPATIBLE:
                die(f"{rel}: SPDX {i} cannot go into the GPL-3.0-or-later firmware")
        if NOTICE.search(text):
            die(f"{rel}: a copyright or licence line names Anthropic or Claude")
    return m


def env_plugins(env):
    """The env's custom_ext_plugins, as PlatformIO resolves it (extends and all)."""
    r = subprocess.run(["pio", "project", "config", "--json-output"], cwd=ROOT,
                       capture_output=True, text=True)
    if r.returncode:
        die("pio project config: " + r.stderr.strip())
    import json
    for section, opts in json.loads(r.stdout):
        if section == f"env:{env}":
            for k, v in opts:
                if k == "custom_ext_plugins":
                    return v.split() if isinstance(v, str) else list(v)
    return []


def main():
    ap = argparse.ArgumentParser(description="Plugins kept in their own repositories")
    ap.add_argument("cmd", choices=["fetch", "check", "list", "env"])
    ap.add_argument("names", nargs="*")
    ap.add_argument("--lock", default=str(LOCK))
    ap.add_argument("--release", action="store_true", help="refuse local paths and working trees")
    a = ap.parse_args()

    if a.cmd == "env":
        if len(a.names) != 1:
            die("env takes one environment name")
        print(" ".join(env_plugins(a.names[0])))
        return
    lock = read_lock(pathlib.Path(a.lock))
    if a.cmd == "list":
        for name, (src, commit) in lock.items():
            print(f"{name:15} {commit[:12]:12} {src}")
        return
    names = a.names or list(lock)
    for name in names:
        if name not in lock:
            die(f"{name} is not in {pathlib.Path(a.lock).name}")
        src, commit = lock[name]
        where = fetch_one(name, src, commit, a.release) if a.cmd == "fetch" else "as it is"
        m = check_one(name, a.release)
        print(f"plugins: {name} {m.get('version', '?')} ({where}), api {m.get('api')}, {m.get('license')}")


if __name__ == "__main__":
    main()
