#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""
===========================================================================
 µnleashed BBS
 Electronic freedom on a microcontroller.
===========================================================================

File:         tools/check_formats.py
Module:       Tools / build checks

Purpose:      Refuses a printf or scanf format in src/ that newlib nano
                 cannot do. Every board builds with
                 CONFIG_NEWLIB_NANO_FORMAT=y (1.1.2, about 70 KB of flash
                 back), and nano is C89 formatted I/O: no %ll, %hh, %j, %z,
                 %t, and no positional %n$. None of that is a compile error
                 on the board, and the host build runs glibc, where all of
                 it works, so no host test can ever see the difference. On
                 the board nano reads one length letter, so %llu prints
                 "lu", takes no argument, and every argument after it moves
                 one place. This check is the only thing standing between
                 such a format and a board.

                 Floats are refused too, though on IDF 5.3.1 they print:
                 the IDF's newlib_init.c names _printf_float whenever nano
                 is on, which links nano's float formatting in. Nothing in
                 src/ needs it, and it works only through that side door.

                 What it reads: every string literal outside comments, with
                 adjacent literals joined the way the compiler joins them.
                 A literal that is an argument to strftime, clk::fmt or
                 clk::fmtEpoch is a time format, where %a and %e are
                 legitimate, and is skipped. Everything else is parsed as a
                 printf format:
                   - a C99 length (ll hh j z t L q) on any conversion;
                   - a positional argument (%1$s);
                   - a wide character (%lc, %ls) or %m, %C, %S;
                   - a float conversion (f F e E g G a A). Prose with a
                     percent sign in it ("5% at") is not a format, so a
                     float conversion carrying the space flag is only
                     refused when the literal sits in a printf-family call
                     (the same for %m, %C and %S). Inside such a call every
                     literal is read as a format, not only the first, so
                     "%s" with "5% at" as its argument is refused: reword
                     it or pass it through a variable.
                 And every identifier in code: the <inttypes.h> PRI and SCN
                 macros are refused outright, since PRIu64 is "llu".

Usage:        python3 tools/check_formats.py [dir ...]   (default: src)
                 Exit 0 when clean, 1 with one line per hit otherwise.
                 Run by `make test` in host/ and by tools/release.py.

Copyright 2026 - Robert Mech
License:      GNU General Public License v3 or later

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
import re
import sys

EXTS = (".c", ".cc", ".cpp", ".h", ".hpp")

# Calls whose string arguments are strftime formats, not printf ones.
TIME_CALLS = {"strftime", "fmt", "fmtEpoch"}

# Calls known to take a printf or scanf format. A literal inside one of
# these gets the strict float rule; everything else gets the prose-safe one.
PRINTF_CALLS = {
    "printf", "sprintf", "snprintf", "vsnprintf", "vsprintf", "fprintf",
    "vfprintf", "vprintf", "asprintf", "dprintf", "log", "note",
    "scanf", "sscanf", "fscanf", "vsscanf",
    "ESP_LOGE", "ESP_LOGW", "ESP_LOGI", "ESP_LOGD", "ESP_LOGV",
    "ESP_EARLY_LOGE", "ESP_EARLY_LOGW", "ESP_EARLY_LOGI",
    "ESP_EARLY_LOGD", "ESP_EARLY_LOGV", "esp_rom_printf", "ets_printf",
}

# One printf conversion: flags, width, precision, length, conversion.
SPEC = re.compile(
    r"%(?P<pos>\d+\$)?(?P<flags>[-+ #0']*)(?P<width>\*(?:\d+\$)?|\d+)?"
    r"(?:\.(?P<prec>\*(?:\d+\$)?|\d+))?"
    r"(?P<len>hh|ll|[hljztLq])?(?P<conv>[diouxXcspnfFeEgGaAmCS%\[])")
C99_LEN = {"hh", "ll", "j", "z", "t", "L", "q"}
FLOATS = set("fFeEgGaA")
GNU_WIDE = set("mCS")        # %m, %C, %S: glibc and C99 extras nano has not got
INTTYPES = re.compile(r"^(PRI|SCN)[diouxX](8|16|32|64|MAX|PTR|LEAST\d+|FAST\d+)$")


def lex(text):
    """Yields ("str", value, line, call) for each run of adjacent string
    literals and ("id", name, line, None) for each identifier outside
    comments and literals. call is the identifier in front of the innermost
    open parenthesis at the literal, or None."""
    i, n, line = 0, len(text), 1
    stack = []          # identifier before each open "(" (or None)
    last_id = None      # last identifier seen, for the next "("
    pend = None         # [value, line, call] of a literal run being joined
    while i < n:
        c = text[i]
        if c == "\n":
            line += 1
            i += 1
            continue
        if c in " \t\r\f\v\\":
            i += 1
            continue
        if text.startswith("//", i):
            j = text.find("\n", i)
            i = n if j < 0 else j
            continue
        if text.startswith("/*", i):
            j = text.find("*/", i + 2)
            j = n if j < 0 else j + 2
            line += text.count("\n", i, j)
            i = j
            continue
        # String literal, with an optional encoding prefix and raw strings.
        # Identifiers are consumed whole below, so a prefix here is always
        # the start of a token: FOO"%llu" is FOO, then a literal.
        m = re.compile(r'(u8|u|U|L)?(R)?"').match(text, i)
        if m:
            start_line = line
            j = m.end()
            if m.group(2):                               # R"delim( ... )delim"
                k = text.find("(", j)
                end = text.find(")" + text[j:k] + '"', k) if k >= 0 else -1
                if end < 0:                              # malformed: the rest is the literal
                    val, j = text[j:], n
                else:
                    val = text[k + 1:end]
                    j = end + (k - j) + 2
            else:
                buf = []
                while j < n and text[j] != '"':
                    if text[j] == "\\" and j + 1 < n:
                        buf.append(text[j:j + 2])
                        j += 2
                        continue
                    buf.append(text[j])
                    j += 1
                val = "".join(buf)
                j += 1
            line += text.count("\n", i, j)
            if pend is None:
                pend = [val, start_line, stack[-1] if stack else None]
            else:
                pend[0] += val
            i = j
            last_id = None
            continue
        # A number, whole: 1'000'000's digit separators are not char literals.
        if c.isdigit() or (c == "." and i + 1 < n and text[i + 1].isdigit()):
            m = re.compile(r"\.?\d(?:[eEpP][+-]|'(?=\w)|[\w.])*").match(text, i)
            i = m.end()
            if pend is not None:
                yield ("str", pend[0], pend[1], pend[2])
                pend = None
            last_id = None
            continue
        if c == "'":                                     # char literal
            j = i + 1
            while j < n and text[j] != "'":
                j += 2 if text[j] == "\\" else 1
            i = j + 1
            last_id = None
            continue
        # Anything else ends a run of adjacent literals, except an identifier
        # that is a macro between two literals ("online " IPSTR " %u"): the
        # run is flushed there, and the pieces are checked on their own.
        if pend is not None:
            yield ("str", pend[0], pend[1], pend[2])
            pend = None
        if c.isalpha() or c == "_":
            m = re.compile(r"[A-Za-z_]\w*").match(text, i)
            last_id = m.group(0)
            yield ("id", last_id, line, None)
            i = m.end()
            continue
        if c == "(":
            stack.append(last_id)
        elif c == ")":
            if stack:
                stack.pop()
        elif c in ";{}":
            stack.clear()
        last_id = None
        i += 1
    if pend is not None:
        yield ("str", pend[0], pend[1], pend[2])


def check_literal(val, call):
    """The reasons this literal would misbehave under nano, as strings."""
    if call in TIME_CALLS:
        return []
    strict = call in PRINTF_CALLS
    bad = []
    for m in SPEC.finditer(val):
        spec, conv = m.group(0), m.group("conv")
        if conv == "%":
            continue
        if m.group("pos") or "$" in (m.group("width") or "") or "$" in (m.group("prec") or ""):
            bad.append(f"{spec}: positional argument")
        if m.group("len") in C99_LEN:
            bad.append(f"{spec}: C99 length '{m.group('len')}'")
        prose_safe = strict or " " not in m.group("flags")
        if conv in FLOATS and prose_safe:
            bad.append(f"{spec}: floating point")
        if conv in GNU_WIDE and prose_safe:
            bad.append(f"{spec}: not a C89 conversion")
        if m.group("len") == "l" and conv in "cs":
            bad.append(f"{spec}: wide character")
    return bad


def scan(path):
    with open(path, encoding="utf-8", errors="replace") as f:
        text = f.read()
    hits = []
    for kind, val, line, call in lex(text):
        if kind == "id":
            if INTTYPES.match(val):
                hits.append((line, f"{val}: <inttypes.h> macro"))
            continue
        for why in check_literal(val, call):
            hits.append((line, why))
    return hits


# The check's own cases: each source snippet and how many hits it must give.
# Run with --self-test (make test does), so a change to the lexer that stops
# it seeing a %llu fails here rather than passing every tree silently.
SELF_TEST = [
    ('snprintf(b, n, "Oldest %04llu", x);', 1),
    ('snprintf(b, n, "%zu %hhu %jd %td %Lf", a, b, c, d, e);', 6),   # %Lf is a length and a float
    ('snprintf(b, n, "%2$s %1$s", a, b);', 2),
    ('snprintf(b, n, "%.1f%%", pct);', 1),
    ('plat::log("took % f s", t);', 1),                 # strict inside a log call
    ('const char* f = wide ? "%-20llu" : "%llu";', 2),  # a format in a variable
    ('const char* g = "%.2f";', 1),
    ('snprintf(b, n, "%l" "lu", x);', 1),               # joined like the compiler
    ('uint64_t v; printf("%" PRIu64, v);', 1),
    ('snprintf(b, n, "%u%% free, %lu, %-*.*s %0*lx %c %p", a, b, w, w, s, 8, h, c, p);', 0),
    ('clk::fmt(when, sizeof(when), wide ? "%a %d %b %e" : "%H:%M");', 0),
    ('strftime(buf, sizeof(buf), "%Y-%m-%d %a", &lt);', 0),
    ('b.rowText(s, Color::Grey, "Brightness 5% at night, 50% of the time");', 0),
    ('// snprintf(b, n, "%llu", x); in a comment', 0),
    ('/* "%llu" */ char c = \'"\'; const char* h = "say \\"%u\\"";', 0),
    ('sscanf(line, "%7s %63s", a, b);', 0),
    ('snprintf(b, n, FOO"%llu", x); snprintf(b, n, "%llu", y);', 2),   # a macro before a literal
    ("int k = 1'000; snprintf(b, n, \"%llu\", x); char c = 'a';", 1),  # digit separators
    ('snprintf(b, n, "%ls %lc %m %S", w, c, s);', 4),
    ('b.rowText(s, Color::Grey, "up 5% more, 3% Cold");', 0),          # prose, not formats
    ('snprintf(b, n, R"x(%llu)x", v);', 1),
    ('const char* r = R"(unterminated %llu', 1),
]


def self_test():
    bad = 0
    for src, want in SELF_TEST:
        got = 0
        for kind, val, _line, call in lex(src):
            if kind == "id":
                got += 1 if INTTYPES.match(val) else 0
            else:
                got += len(check_literal(val, call))
        if got != want:
            print(f"formats self-test: {got} hit(s), wanted {want}: {src}")
            bad += 1
    if bad:
        return 1
    print(f"formats self-test: {len(SELF_TEST)} cases pass")
    return 0


def main(argv):
    if argv[1:2] == ["--self-test"]:
        return self_test()
    roots = argv[1:] or [os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "src")]
    total = 0
    files = 0
    for root in roots:
        for dirpath, _, names in os.walk(root):
            for name in sorted(names):
                if not name.endswith(EXTS):
                    continue
                files += 1
                p = os.path.join(dirpath, name)
                for line, why in scan(p):
                    print(f"{os.path.relpath(p)}:{line}: {why}")
                    total += 1
    if total:
        print(f"formats: {total} format(s) newlib nano cannot print "
              "(CONFIG_NEWLIB_NANO_FORMAT, see tools/check_formats.py)")
        return 1
    print(f"formats: {files} files, no format newlib nano cannot print")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
