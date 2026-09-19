<!--
µnleashed BBS: USERS.md

Signing up, logging in, guests, and managing user accounts.

Copyright 2026 - Robert Mech
License: GNU General Public License v2 or later
SPDX-License-Identifier: GPL-2.0-or-later

Documentation for µnleashed BBS, part of the same distribution as the
source. See the LICENSE file for terms.
-->

# µnleashed BBS: user accounts

How callers get accounts, and how staff create, change, lock and delete them. Commands and keys are also listed in [COMMANDS.md](COMMANDS.md).

## Limits

- Up to 250 accounts on the board's own flash (`max_users`, 1..250).
- The cap is an index width, not a space limit: the `userdata` partition holds
  roughly 1,380 accounts at about 450 bytes each. The list indices are `uint8_t`
  and one of them counts rows in every list on the board, so raising the cap is
  a change to all of them rather than a change to this number.
- Accounts never move to the SD card. They are the one thing that has to survive
  a card failing, and LittleFS on the board is power-fail safe in a way FAT is not.
- Accounts live in `users.txt` on the storage partition and travel in the backup zip ([BACKUP.md](BACKUP.md)).

## Callers

### Before the password

Nobody on this board types a password before being told what happens to it. Pressing `R` at the handle prompt does not open the sign-up form straight away. It says, in yellow:

```
This connection is not encrypted. Use a password
you do not use anywhere else.
Would you like to know more? [Y/N]
```

`N` opens the form. `Y` plays `screens/privacy.*` first, which is the full disclosure: telnet carries everything in the clear, the password is salted and hashed with SHA-256 a thousand rounds so the stored file gives nothing away, the sysop can see handles, addresses, times, the last command run and can watch a node live, and the honest summary of what any of that is worth. The board is conversation and nothing critical, and the one real risk is a password reused from somewhere else.

The form opens when the screen finishes either way.

`PRIVACY` shows the same screen at any time, so a caller who agreed to something months ago can read it again without making a new account. Being an ordinary screen file, a sysop can rewrite it to match their own board: see [SCREENS.md](SCREENS.md).

### Signing up

Everyone starts at the same handle prompt. A handle the BBS doesn't know asks what to do:

```
Enter your handle: Rob
Rob is new here.
[R]egister, [G]uest or [N]ew handle?
```

- `R` opens the sign-up form (only offered with `self_register = yes`, the default, and while `max_users` isn't reached).
- `G` logs in as a guest under that handle (only offered with `guest = yes`, the default). See Guests below.
- `N`, ESC or Ctrl-C go back to the handle prompt.

| Field | Rules | Who sees it |
|---|---|---|
| Handle | from the prompt: letters, digits, space `-` `_` `.`, up to 20, starts with a letter or digit, not `SYSOP` | everyone |
| Password, Again | 4 to 32 characters, typed twice, shown as `*` | nobody |
| Name | required, up to 32 | everyone |
| Email | required, must look like `a@b.c`, up to 64 | you and staff with `USERS` |
| Address | optional, up to 64 | you and staff with `USERS` |
| Phone | optional, up to 20 | you and staff with `USERS` |
| Profile | optional, 4 rows of 37 (148 characters) | everyone |

"Who sees it" is the whole story for callers but not for a sysop: see
[Who can actually see your details](#who-can-actually-see-your-details) below,
which covers staff, backups and the fact that telnet is plaintext.

Handles match without regard to case, so `rob` and `ROB` are the same account. The BBS always shows the spelling it was created with.

### Who can actually see your details

This matters more than a column in a table, so it is spelled out.

**Other callers cannot see your email, address or phone.** `INFO handle` shows
another caller their name, profile, when they joined, their last call and their
staff level, and nothing else. The three private fields are skipped unless you
are looking at your own account or you hold the `USERS` permission.

**Staff can.** Anyone with `USERS` sees every field of every account, because
that is what running a board requires. By default that is the sysop and
co-sysop 1, not co-sysop 2, and it is a row in the `[access]` matrix a sysop
can change. There is no setting that hides a caller's details from the person
who runs the board, and there should not be: pretending otherwise would be
worse than saying it.

**A backup zip contains all of it in the clear.** The download redacts the
three staff passwords and nothing else, so `users.txt` inside it carries every
caller's email, address and phone as typed. Whoever can open the backup window
can read them. That is why the backup port refuses connections from anywhere
but a private address and why the window closes on a timer.

**Passwords are not in there in any readable form.** They are stored as a
salted SHA-256 run a thousand times. That is not state of the art and is not
meant to be; it means a stolen `users.txt` does not hand over the passwords,
and the file has to be stolen first.

**Nothing typed into this board is private in transit.** Telnet has no
encryption, so everything, including the password at the prompt, crosses the
network as readable text. That is the price of letting a 1982 computer call in
and it is not going to change. The practical rule for a caller is the one the
sign-up screen gives them: use a password you use nowhere else, and do not type
anything here you would mind the sysop, or somebody on the same network,
reading.

A sysop putting a board on the public internet is taking on other people's
email addresses and phone numbers. Collect the ones you actually need.

With `self_register = no`, only `[G]uest` is offered, and staff add accounts. With guests off too, an unknown handle is refused in place: `No account. The sysop creates accounts here.`

When `max_users` is reached, sign-ups are refused with `Sign-ups are closed: the BBS is full.`

### Form keys

On ANSI and PETSCII terminals the form is a full screen with boxes:

| Key | Effect |
|---|---|
| Up / Down (C64: CRSR) | previous / next field |
| Enter (C64: RETURN) | next field; on `Save` or `Cancel`, do that |
| Left / Right | move between `Save` and `Cancel` |
| F1 | save from any field |
| Backspace (C64: INST/DEL) | delete the last character |
| ESC or Ctrl-C (C64: left-arrow, RUN/STOP) | cancel, nothing is saved |
| Y / N / Space | set a yes/no field (Locked) |

Plain ASCII terminals get one line per field instead. Enter on an empty line keeps the value shown in brackets, and the last question is `Save (Y/n)?`.

If a value is wrong, the form beeps, names the problem and puts you back on that field.

### Logging in

```
Enter your handle: Rob
Password: ACCESS GRANTED
Welcome back, Rob!
```

The stars you type spin, rub out and turn into `ACCESS GRANTED` on the same line. A wrong password flashes `ACCESS DENIED` there, clears, and you type again on that line.

- 3 wrong passwords on one call hang up the line.
- 5 wrong passwords for one handle within 15 minutes lock that handle for 15 minutes, whichever line they come from. The lock is in RAM and clears on reboot.
- ESC at `Password:` goes back to the handle prompt.
- A handle the sysop locked is refused before the password: `This account is locked. Ask the sysop.`

The daily time limit (`day_minutes`) is counted per account and survives logoffs. Each logoff adds the call to the account's call count and minutes for the day.

### Guests

A handle with no account, then `G`, gets in without an account (`guest = yes`, the default).

- The guest keeps the handle they typed. Lists mark guests with `*` and a `* guest` footnote:

```
 Who's online              Thu 17 Sep 16:22:37
N Handle       Terminal   Min  Idle
1 Alice        ANSI-UTF8    0 00:10
2 Wanderer*    PETSCII-40   0 00:00
---------------------------------------
* guest
```

- Nobody else can use that handle while the guest is on. Afterwards it is free, and anyone can register it.
- Nothing is saved. There is no account to edit, so `PROFILE` and `PASSWORD` don't exist for guests.
- Guests can't become staff: `BYE <password>` from a guest just logs off.
- A guest call lasts `guest_minutes` (15 by default) with warnings at 5 and 1 minute left. There is no daily limit.
- The call is still listed in `LAST`, like every call.
- To keep what they do, a guest signs up next time with a handle of their own.

### Staff rank on an account

Typing a staff password (`BYE <password>`) also marks that caller's account with the rank. The mark is what the lists show:

| Marker | Meaning |
|---|---|
| (space) | ordinary caller |
| `*` | guest, no account |
| `>` | co-sysop 1 or 2 |
| `]` | sysop |

Every list (WHO, NODES, LAST, DASH, the user manager) prints the marker between the node number and the handle and repeats the key underneath.

Staff may only manage accounts at their own rank or below:

- A co-sysop 1 can edit, lock, rename or delete callers and co-sysops, never the sysop's account.
- The `Level` field in the add and edit forms only offers their own rank and below, so nobody can promote themselves.
- To demote someone, set `Level` back to `User` in `USER EDIT`.

### Your account

| Command | What it does |
|---|---|
| `PROFILE` | Form with your name, email, address, phone and profile. The handle can't be changed here. |
| `PASSWORD` | Form: current password, new password twice. A wrong current password counts toward the handle lock. |
| `INFO` or `I` | Your account: all fields, member since, last call, number of calls, profile. |
| `INFO handle` | Another caller's account. Email, address and phone are hidden unless you hold `USERS`. |

## Staff

Staff access still comes from `BYE <password>` ([COMMANDS.md](COMMANDS.md#staff-sysop-and-co-sysops)), not from an account. The sysop and co-sysops sign up and log in like any caller first, then elevate. A guest session can't elevate. Keeping the two apart means a guessed account password never grants staff rights.

The `USERS` permission (sysop always, co-sysop 1 by default, not co-sysop 2) grants everything below.

### User manager

`USERS` opens a full-screen list on ANSI and PETSCII terminals:

```
USER MANAGER                   3 of 100
───────────────────────────────────────
 Handle        Name             Calls
 Rob           Rob Mech           12
 Alice         Alice Liddell       3
 Mallory       M                   1 L
───────────────────────────────────────
Enter edit  A add  D delete  Q quit
```

`L` marks a locked account.

| Key | Effect |
|---|---|
| Up / Down | move the highlight (the list scrolls) |
| Enter or E | edit the highlighted account |
| A | add an account |
| D | delete the highlighted account, after `Delete handle (y/N)?` |
| Q, ESC or Ctrl-C | back to the prompt |

After a save or delete, the list redraws with the result under it.

Plain ASCII terminals get a paged list and use the typed commands below.

### Typed commands

These work on every terminal.

| Command | What it does |
|---|---|
| `USER ADD` | Add-account form: handle, password, the account fields, Level, Locked. |
| `USER EDIT handle` | Edit-account form. Leave `New pass` empty to keep the password. |
| `USER DEL handle` | Delete after `Delete handle (y/N)?`. `N`, Enter or ESC keeps it. |

- Staff can rename an account in the edit form. A caller who is online under the old handle keeps the session under the new one.
- Setting `Locked` to `Y` refuses the next login. It does not drop a caller who is already on (use `KICK`).
- You can't delete the account you're logged in with.
- Staff never see passwords. To help a caller who forgot theirs, set a new one with `USER EDIT` and tell them.
- A handle in use by an online guest cannot be added, renamed to, or signed up for until that guest leaves.

## Settings

In `system.cfg`:

| Key | Default | Meaning |
|---|---|---|
| `self_register` | `yes` | `no`: only staff can add accounts |
| `max_users` | `100` | account limit, 1..100 |
| `guest` | `yes` | `no`: unknown handles are not offered `[G]uest` |
| `guest_minutes` | `15` | per guest call, 0 = unlimited, no daily limit |

## users.txt

Normally you never touch this file. It comes down in the backup zip, and you can edit it there and upload it back. The upload is checked before the sysop is asked: a `users.txt` with any problem is listed as rejected with the reason, and the accounts on the board stay as they are. The rest of the upload can still be applied.

```
# µnleashed BBS users. Edit through the backup zip, see USERS.md.

[Rob]
name = Rob Mech
email = rob@example.com
address =
phone =
profile = Plays chess on a C64
pass = 3f9a0c1d2e4b5a69$5d1e...(64 hex)
level = user
created = 1789000000
last_call = 1789100000
calls = 12
day = 2026260
day_minutes = 45
locked = no
```

- One `[handle]` block per account. The handle rules above apply, and duplicates are refused.
- `level` is `user`, `co2`, `co1` or `sysop`, and drives the list markers and who may manage the account.
- Values keep their spaces. Only the single space after `=` belongs to the format.
- `pass` is a random 8-byte salt and a SHA-256 hash (repeated 1000 times), both in hex. You can't type a password into the file; set passwords on the BBS. An empty `pass` means nobody can log in to that account until staff set one.
- `locked = yes` locks the account.
- Keys the BBS doesn't know are accepted with a warning that names the line ("line 12: unknown key 'nickname'"), and dropped the next time the file is written.
- Numbers that aren't numbers, values longer than the field, and a `pass` that isn't a salt and hash are refused, each naming its line.
- `created` and `last_call` are Unix times; `day` and `day_minutes` track the daily limit.
- Uploading a zip without `users.txt` leaves the accounts on the board as they are.
- More accounts than `max_users` is refused.

The hash is there so a copy of the backup zip doesn't hand out passwords. It is not strong protection against someone who has the file and time to guess, so keep backups private, and tell callers not to reuse a password from elsewhere. Telnet sends passwords in the clear anyway.
