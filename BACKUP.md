<!--
µnleashed BBS: BACKUP.md

Downloading and uploading config, accounts and screens as one zip, and
keeping it on the SD card (BACKUP SD, RESTORE SD, the nightly backup).

Copyright 2026 - Robert Mech
License: GNU General Public License v2 or later
SPDX-License-Identifier: GPL-2.0-or-later

Documentation for µnleashed BBS, part of the same distribution as the
source. See the LICENSE file for terms.
-->

# Backup and restore

Everything that matters on the board travels as one `.zip`: `system.cfg`, the user accounts, the information pages and every screen. You download it, change what you want, and upload it back. Logs are not in the zip and are never touched by a restore.

The zip spans two partitions. `system.cfg`, `users.txt` and the information pages live on `userdata`, which a firmware or filesystem upload never touches; the screens live on `storage`, which a filesystem upload replaces. A restore puts each file back where it belongs, the zip format itself is unchanged, and a backup taken from an older board still restores correctly.

No USB cable, no web browser, no reflashing. Just the BOOT button and `curl`. A board with an SD card can also keep the same zip on the card and take it back from there, from the sysop's prompt: see [Backups on the SD card](#backups-on-the-sd-card).

## 1. Open the backup window

1. Log in to the BBS and become sysop: `BYE <sysop_password>`.
2. Press the BOOT button on the board (the one that is not EN/RESET).
3. The sysop console shows:

```
*** Backup open 5 min: http://192.168.1.50:8080/backup.zip
*** It holds the Wi-Fi password. Never forward this port.
```

The port answers only local addresses: `10/8`, `172.16/12`, `192.168/16`, `127/8`, link-local `169.254/16`, and `100.64/10`, which is where Tailscale and carrier NAT put you. Anything else gets a 403 and the sysop console shows `*** Backup refused <ip>: not a local address`. A VPN still works. The check looks at the source address, so it is a guard rather than a wall: a router that rewrites the source of forwarded traffic to its own LAN address gets past it. Never forward the backup port.

The window stays open for `backup_window_minutes` (default 5), then closes by itself. It also closes if the sysop logs off. Outside the window the port is not listening at all.

If nothing happens, check the serial log: `backup: button pressed, but the sysop is not on the sysop node` means you pressed it before becoming sysop.

For its first 10 seconds after starting, the board reads BOOT as the reset button instead (1.1.0, see "Resetting the board" in [README.md](README.md)) and the backup window ignores it. Nobody can be sysop that soon after a restart, so in practice this only matters if you restart the board and press BOOT straight away: a short press there does nothing at all.

## 2. Download

Windows (PowerShell or cmd, note `curl.exe`, not `curl`):

```
curl.exe -o backup.zip http://192.168.1.50:8080/backup.zip
```

Linux / macOS:

```
curl -o backup.zip http://unleashed.local:8080/backup.zip
```

Use the IP address from the console notice if `unleashed.local` does not resolve (Windows often does not). No confirmation is needed for a download. The sysop console shows `*** Backup downloaded by <ip>`.

## 3. Edit

Windows PowerShell:

```
Expand-Archive backup.zip -DestinationPath backup -Force
notepad backup\system.cfg
```

Linux / macOS:

```
unzip -o backup.zip -d backup
```

What is inside:

| Path | What it is |
|---|---|
| `system.cfg` | all settings, the staff passwords show as `***`; the Wi-Fi password is in it as typed, so a restore onto a fresh board brings the network with it |
| `users.txt` | user accounts, passwords as salted hashes, see [USERS.md](USERS.md#userstxt). The download is taken from a snapshot, so accounts may change while it streams without spoiling the zip. |
| `info/0.txt` to `info/9.txt` | the information pages' text (`INFO`), one file for each page that has any (1.1.0). The pages' titles and levels are in `system.cfg`. |
| `screens/*.asc .ans .seq .p40 .p80` | display files, see [SCREENS.md](SCREENS.md) |
| `MANIFEST.txt` | version, date, file list (ignored on upload) |

Passwords: leave `***` as it is to keep the current password. Type a real password in its place to change it. An empty value disables that staff level.

A restore never writes the published default sysop password. A board still on the default has no `sysop_password` line, and that is what limits the default to your own network and keeps the board off the directory, so `***` on such a board leaves the line out rather than writing the default in. A line that names the default outright is left out too: for the sysop the board is then on the default, for a co-sysop that level is off. When the board is on the default after a restore, curl's `Applied:` line ends with `sysop password is the published default, local network only` and the sysop console shows `*** Sysop password is the published default: local only`; set your own in `CONFIG staff`.

Screens: add, change or delete files in `screens/`. When your upload contains any screens, the board's screens become exactly that set, so a screen you delete from the folder is deleted on the board. An upload with no screens at all leaves the screens alone.

Accounts: an upload with `users.txt` replaces every account with the file's contents; an upload without it leaves the accounts alone. Account passwords can't be typed into the file, only kept or cleared (see [USERS.md](USERS.md#userstxt)).

Information pages: a page in the zip (`info/3.txt`) replaces that page's text on the board. A page that is not in the zip is left as it is, so an older backup, which has none, changes no page. Up to 8 KB a page.

## 4. Re-zip

Zip the files again with any tool. Compressed or not, both work. Zipping the folder itself is fine too (the top folder is ignored).

Windows PowerShell:

```
Compress-Archive -Path backup\* -DestinationPath backup.zip -Force
```

Linux / macOS:

```
cd backup && zip -r ../backup.zip . && cd ..
```

## 5. Upload

The window must be open (press BOOT again if it closed).

Windows:

```
curl.exe -T backup.zip http://192.168.1.50:8080/restore
```

Linux / macOS:

```
curl -T backup.zip http://unleashed.local:8080/restore
```

The board unpacks the zip into a staging area on `userdata` (1.1.0: it was on the screens partition, which was too small for the limits the board advertised) and checks every file. Before it unpacks anything it checks there is room for it, counted in the 4 KB blocks the flash filesystem really uses, and keeps 32 KB back for the accounts. Nothing is live yet. The sysop console then asks:

```
Upload from 192.168.1.20: 15 files, 60 KB, system.cfg, users, screens
nothing rejected, 0 screens removed
Accept upload (Y/N)?
```

Read the IP address before answering: anyone on your network can send an upload while the window is open, only the sysop can accept it.

- `Y` swaps the files in, reloads `system.cfg` and the screens immediately (no reboot), and curl prints `Applied: ...` once the last file is in. The files go in one each loop pass, so callers are not held up while it happens, and curl going away part way does not stop it. When `system.cfg` or an information page came back, the plugins start again on it, as a `CONFIG` save makes them, so a restored plugin setting or page is live too (1.1.0); anyone inside a plugin at that moment is put back at the main prompt.
- `N` throws the upload away and curl prints `Upload discarded`.
- No answer in 2 minutes counts as `N`.

`hostname` changes take effect at the next reboot, and so do the Wi-Fi network and the listening port. Everything else applies at once.

## What curl can print

| Status | Meaning |
|---|---|
| `200 Applied: ...` | accepted by the sysop and live |
| `403 Upload discarded: ...` | sysop said N, did not answer, or logged off |
| `422 Nothing to apply ...` | no file in the zip passed the checks (reason included) |
| `400 rejected: ...` | not a zip, or a damaged one |
| `413 Too big: 300 KB. The limit is 256 KB.` | the zip is over 256 KB (262,144 bytes) |
| `507 Board full: 180 KB needed, 120 KB free.` | the board has no room to take or unpack it right now |
| `411` | sent without a length (use `curl -T`) |
| `503 busy` | someone else is using the window right now |
| connection refused | the window is closed: press BOOT |

Files that fail the checks are listed as rejected and never reach the board. The rules and limits are in [SCREENS.md](SCREENS.md).

## Backups on the SD card

With an SD card fitted, the board can keep its own backups. `BACKUP SD` writes the same zip the backup window gives you, settings, accounts and screens, into a `backup` folder on the card, named for the date and time it was made. `BACKUP SD SCREENS` writes the screens alone. `RESTORE SD` lists what is on the card, newest first, and `RESTORE SD 2` shows what the second one would replace, including how many accounts are on the board now and how many are in the zip, then asks before it changes anything.

A full backup holds the Wi-Fi password as typed, exactly as a downloaded one does, so whoever has the card has the password. It does not hold the staff passwords: a restore always keeps the ones the board has, so after a factory reset, set the sysop password again straight after restoring.

`RESTORE SD SCREENS` is different. It never touches the screens built into the board: it copies the zip's screens onto the card, where they take the place of the built-in ones for as long as they are there. Deleting them from the card puts the built-in ones back.

The details, for the sysop at the prompt (sysop only, like `CONFIG`):

| Command | What it does |
|---|---|
| `BACKUP SD` | `unleashed-YYYYMMDD-HHMM.zip` in `<card>/backup/`: `system.cfg` (staff passwords as `***`), `users.txt`, the information pages, the screens and `MANIFEST.txt`. A dot a file while it writes, then `Saved: 14 files, 31 KB.` |
| `BACKUP SD SCREENS` | `screens-YYYYMMDD-HHMM.zip`, the screens and the manifest only |
| `RESTORE SD` | the zips in the backup folder, newest first and numbered (the newest 16), with their sizes |
| `RESTORE SD n` or `RESTORE SD name.zip` | checks zip n exactly as an upload through the window is checked, says what it would do, and asks `Restore now? (y/N)`. Y puts it back and it is live at once; N, ESC or 60 seconds with no answer is `Not restored.` |
| `RESTORE SD SCREENS n` | the same, for a zip's screens only, onto the card's `screens` folder. It adds and replaces, never removes, and anything in the zip that is not a screen is listed as rejected |

- The question always shows `In zip`, `Replaces`, `Accounts`, `Removes` and `Staff` for a full restore. `Removes` is the count of screens on the board that are not in the zip, which a full restore deletes (the window's rule); it is what tells a full restore of a screens-only zip apart from `RESTORE SD SCREENS`. `Staff` says whether the zip changes any staff password: a backup's `***` keeps the board's own, but a line the zip leaves out, empties or types in does change it. `Wi-Fi` appears when the zip's network is not the board's.
- A number can only name a zip the list shows, and a name only a zip in the backup folder: nothing typed can reach any other file. A name with a space in it is restored by its number.
- Two backups in the same minute would share a name, so the second is refused: `There is one from this minute already.` A zip is written as `name.tmp` and renamed when it is whole, so a card pulled half way leaves nothing the list would offer.
- Writing and restoring go a step at a time, a few kilobytes or one file each loop pass, so callers are not held up while it happens. The zip limits are the window's: 256 KB for the zip, and the board has to have room to unpack it.
- With the `sd` plugin's screens override switched off (`screens = no` in `CONFIG sd`), `RESTORE SD SCREENS` says so: the screens go onto the card but do not play until it is on.
- A screen imported this way is the sysop's own. The card keeps a record (`screens/.seeded`) of the stock screens the board put there, and a later firmware update refreshes only a card copy that still matches that record. An imported screen that is byte for byte the stock one is indistinguishable from the board's copy and follows stock updates like it.

### The nightly backup

`nightly = yes` on `CONFIG sd` (off as shipped) makes a full backup on the card every night at 03:00 local time, named `nightly-YYYYMMDD.zip`, and keeps the last seven. Only files named exactly `nightly-YYYYMMDD.zip` are ever counted or removed, so a backup made by hand is never pruned, however old. It needs a valid clock and the card; if the board was busy with another backup for the whole of the 03:00 hour, or off, that night is skipped rather than made in the middle of the day.

When a night's backup does not happen, the console says why (`backup: nightly skipped: no card`) and staff are told when they next arrive: `Last night's backup failed: no card.` (or `card full`, or `no clock`).

## Settings for the window

In `system.cfg`:

```
backup_port = 8080
backup_window_minutes = 5
backup_button_gpio = 0
```

`backup_button_gpio = -1` disables the button, and with it the whole backup window.

## Security notes

- The window only opens with a physical button press while the sysop is logged in, and closes by itself.
- Downloads are not confirmed. The zip never contains staff passwords, only `***`. Account passwords are in it as salted hashes. **The Wi-Fi password is in it in the clear**, on purpose, so keep backups private. That includes the zips `BACKUP SD` and the nightly backup leave on the SD card: whoever holds the card holds the password.
- Only local addresses can connect. Never forward the backup port.
- Uploads always need the sysop's Y.
- Plain HTTP, like the BBS itself is plain telnet. Use it on your own network.
