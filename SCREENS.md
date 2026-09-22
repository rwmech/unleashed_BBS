<!--
µnleashed BBS: SCREENS.md

Screen file formats, naming rules, @-codes and upload limits.

Copyright 2026 - Robert Mech
License: GNU General Public License v2 or later
SPDX-License-Identifier: GPL-2.0-or-later

Documentation for µnleashed BBS, part of the same distribution as the
source. See the LICENSE file for terms.
-->

# Screens: formats, rules and limits

Screens are the files in the backup zip under `screens/`. Edit them, upload them with the backup window ([BACKUP.md](BACKUP.md)), and the BBS uses them immediately.

## Names

`screens/<name>.<ext>`

- `<name>`: 1 to 8 characters, lowercase `a-z`, digits, `_` or `-`.
- `<ext>`: one of `asc` `ans` `seq` `p40` `p80`.
- Anything else in the zip is rejected on upload: other folders, other extensions, long or uppercase names.

The names the BBS looks for:

| Name | Shown |
|---|---|
| `welcome` | after terminal detection |
| `about` | the `ABOUT` command |
| `privacy` | the disclosure offered at sign-up and shown by `PRIVACY`. Rewrite it to match your board, but keep it honest: callers are deciding what password to type |

## Page breaks

A screen can stop deliberately, rather than only when it has filled the
terminal. A **form feed** byte (`0x0C`) in the file means: wait for a key,
clear the screen, carry on. That turns a long screen into a short document
somebody will actually read, instead of a wall sliding past.

Ordinary paging still applies to anything that overflows: a screen that
fills the terminal shows `[More]` as before. The two do not fight.

`screens/privacy.*` is the worked example, four pages with its own
`Page 1 of 4` markers in the text. Nothing counts pages for you: if you
rewrite that screen, write your own numbers, or leave them out.

`tools/mkscreens.py` generates it, and is the easiest place to start from
if you want to write your own version for your board.
| `motd` | after login (optional) |
| `busy` | when every node is in use |
| `goodbye` | at logoff |

There is no help screen: `HELP` is generated from the command table so it always matches the commands a caller can use.

## Formats

The BBS picks the best file for each caller, first match wins:

| Caller | Tries |
|---|---|
| C64 40 columns | `.p40` `.seq` `.asc` |
| C128 80 columns | `.p80` `.seq` `.asc` |
| ANSI (PC) | `.ans` `.asc` |
| Plain ASCII | `.asc` |

- `.asc` plain ASCII text. Translated for every terminal, so one `.asc` covers everyone. Long ones pause with `[More]`.
- `.ans` ANSI art in CP437 (the classic PC BBS character set). UTF-8 callers get it converted automatically. SAUCE records are skipped.
- `.seq` / `.p40` / `.p80` raw PETSCII for Commodore callers.

Art files (`.ans`, `.seq`) are drawn with cursor movement and are never paged, so keep them to one screen.

## Layout rules

- Commodore and plain ASCII screens: at most 39 characters per line. A 40th character wraps on a C64 and adds a blank line.
- ANSI screens: 80 columns, 23 lines so the prompt fits.
- Keep `.asc` to plain ASCII. The one exception is `@BBS@`, which prints the name with a real µ where the terminal can show it.

## @-codes

Work in every format, upper or lower case:

| Code | Prints |
|---|---|
| `@BBS@` | the BBS name |
| `@VER@` | version |
| `@NODE@` `@NODES@` | caller's node, number of nodes |
| `@USER@` | caller's handle |
| `@TERM@` `@COLS@` | terminal type, columns |
| `@DATE@` `@TIME@` | local date and time |
| `@CLS@` | clear screen |
| `@BELL@` | bell |
| `@DELAY:ms@` | pause, e.g. `@DELAY:500@` |
| `@SPIN:ms@` | spinner for that long |

## Limits (enforced on upload)

| Limit | Value | Why |
|---|---|---|
| One file, unpacked | 64 KB | keeps one screen from eating the storage |
| Files per upload | 64 | fixed table on the board |
| All files together, unpacked | 360 KB | the board stages a full copy before swapping it in |
| The `.zip` itself | 400 KB | same reason |
| `system.cfg` | must pass the config checks | a bad config never replaces a working one |

Storage on the board is 768 KB. Staging needs room for a second copy, which is why the total is about half of that.

A file over a limit is rejected with the reason and the rest of the upload still goes through. The sysop sees the count of rejected files before answering Y/N.

## Logs

Logs (the caller log behind `LAST`) live on their own 128 KB partition. They are fixed-size rings that cannot grow, they are not in the zip, and a restore never touches them.

## Regenerating the stock screens

Optional screens, each of which the board simply skips when the file is
absent:

| Screen | When it plays |
|---|---|
| `rules` | pressing R to register, before anything is typed. Two pages. |
| `newuser` | once a registration succeeds, in place of the motd |
| `chatin` | joining the chat room |
| `files` | entering the file subsystem, above the area menu |
| `goodbye` | every ending: BYE, idle, out of time, kicked or banned |

`files` is a door rather than a page: the area menu is drawn underneath it,
so it has to leave room. Budget about eight rows at 80 columns and seven at
40, which keeps the menu and its prompt on screen even with ten areas.
Entering the subsystem clears the screen first, and the menu deliberately
does not clear again afterwards, because wiping a screen that has just
played is the same as not having one.

`goodbye` is followed by a five second hold (`BBS_EXIT_LINGER_MS`) so the
screen is not cut off by the socket closing.

`tools/mkscreens.py` rebuilds the stock set in `data/screens/`. Those are only used for a fresh board (`pio run -t flashall`). Day to day, edit screens through the backup zip.
