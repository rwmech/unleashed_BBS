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
| `bulletin` | after login (optional) |
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

`tools/mkscreens.py` rebuilds the stock set in `data/screens/`. Those are only used for a fresh board (`pio run -t flashall`). Day to day, edit screens through the backup zip.
