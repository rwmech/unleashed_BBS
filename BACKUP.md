<!--
µnleashed BBS: BACKUP.md

Downloading and uploading config, accounts and screens as one zip.

Copyright 2026 - Robert Mech
License: GNU General Public License v2 or later
SPDX-License-Identifier: GPL-2.0-or-later

Documentation for µnleashed BBS, part of the same distribution as the
source. See the LICENSE file for terms.
-->

# Backup and restore

Everything that matters on the board travels as one `.zip`: `system.cfg`, the user accounts and every screen. You download it, change what you want, and upload it back. Logs are not in the zip and are never touched by a restore.

The zip spans two partitions. `system.cfg` and `users.txt` live on `userdata`, which a firmware or filesystem upload never touches; the screens live on `storage`, which a filesystem upload replaces. A restore puts each file back where it belongs, the zip format itself is unchanged, and a backup taken from an older board still restores correctly.

No USB cable, no web browser, no reflashing. Just the BOOT button and `curl`.

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
| `screens/*.asc .ans .seq .p40 .p80` | display files, see [SCREENS.md](SCREENS.md) |
| `MANIFEST.txt` | version, date, file list (ignored on upload) |

Passwords: leave `***` as it is to keep the current password. Type a real password in its place to change it. An empty value disables that staff level.

Screens: add, change or delete files in `screens/`. When your upload contains any screens, the board's screens become exactly that set, so a screen you delete from the folder is deleted on the board. An upload with no screens at all leaves the screens alone.

Accounts: an upload with `users.txt` replaces every account with the file's contents; an upload without it leaves the accounts alone. Account passwords can't be typed into the file, only kept or cleared (see [USERS.md](USERS.md#userstxt)).

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

The board unpacks the zip into a staging area and checks every file. Nothing is live yet. The sysop console then asks:

```
Upload from 192.168.0.20: 15 files, 60 KB, system.cfg, users, screens
nothing rejected, 0 screens removed
Accept upload (Y/N)?
```

Read the IP address before answering: anyone on your network can send an upload while the window is open, only the sysop can accept it.

- `Y` swaps the files in, reloads `system.cfg` and the screens immediately (no reboot), and curl prints `Applied: ...`.
- `N` throws the upload away and curl prints `Upload discarded`.
- No answer in 2 minutes counts as `N`.

`hostname` changes take effect at the next reboot. Everything else applies at once.

## What curl can print

| Status | Meaning |
|---|---|
| `200 Applied: ...` | accepted by the sysop and live |
| `403 Upload discarded: ...` | sysop said N, did not answer, or logged off |
| `422 Nothing to apply ...` | no file in the zip passed the checks (reason included) |
| `400 rejected: ...` | not a zip, or a damaged one |
| `413` | the zip is over 400 KB |
| `411` | sent without a length (use `curl -T`) |
| `503 busy` | someone else is using the window right now |
| connection refused | the window is closed: press BOOT |

Files that fail the checks are listed as rejected and never reach the board. The rules and limits are in [SCREENS.md](SCREENS.md).

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
- Downloads are not confirmed. The zip never contains staff passwords, only `***`. Account passwords are in it as salted hashes. **The Wi-Fi password is in it in the clear**, on purpose, so keep backups private.
- Only local addresses can connect. Never forward the backup port.
- Uploads always need the sysop's Y.
- Plain HTTP, like the BBS itself is plain telnet. Use it on your own network.
