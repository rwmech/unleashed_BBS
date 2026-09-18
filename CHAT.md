<!--
 ===========================================================================
  µnleashed BBS
  Electronic freedom on a microcontroller.
 ===========================================================================

 File:         CHAT.md
 Module:       Documentation / chat and messages

 Purpose:      The chat room and the message system: what a caller sees,
               every room command, moderation, and the settings a sysop
               can change.

 Audience:     Callers and sysops. The code lives in src/plugins/chat.cpp.

 Copyright 2026 - Robert Mech
 License:      GNU General Public License v2 or later
 SPDX-License-Identifier: GPL-2.0-or-later
 ===========================================================================
-->

# µnleashed BBS: chat and messages

The chat room is one room, the way DDial and Gtalk did it. Everybody who joins sees every line as it is said, each line carries who said it and which node they are on, and nothing scrolls past that a caller has not seen.

`CHAT` joins. `/q` leaves. That is the whole thing.

## What a line looks like

```
#2:Daytona) evening all
```

- `#2` is the node. The sysop's hidden node shows as `#S`.
- `:` and the bracket are punctuation, in their own colour.
- `Daytona` is the handle.
- The bracket says the rank: `)` a caller, `*` a guest, `>` a co-sysop, `]` the sysop.

Room notices (`*** #2:Daytona) joined`) are in their own colour and are never hidden by anything. A private line arrives with a `>` in front of it.

## Typing

There is no prompt character. The cursor sits at the start of the line, and what you type stays where you put it: **lines said by other people while you are part way through yours are held until you press Enter**. That is per caller, not for the room. Everyone else sees the room live while you type.

Nothing is dropped. The room keeps a buffer (48 lines by default) and a caller who joins sees the last few lines of it, dimmed. If somebody types for long enough that the held lines are close to filling the buffer, their own line is lifted off the screen, the room prints underneath, and their line is put back so they can carry on.

Callers are rate limited: eight lines in a burst, then 80 lines a minute, which is faster than anyone types. Only the caller who trips it is told; the room never sees the message.

## Room commands

A line that starts with `/` is a command, not something you said.

| Command | What it does |
|---|---|
| `/?` | this list, with the staff lines when you are staff |
| `/s` | who is in the room, with any away notes |
| `/p n text` | one line to node n only |
| `/me text` | an action line: `#1:Daytona) * waves` |
| `/a [note]` | away with a note, or back again when the note is left off |
| `/sq n` | hide node n's lines for this call, or show them again |
| `/t` | the time |
| `/clear` | wipe your screen and start fresh |
| `/email h text` | leave a message for a caller to read later |
| `/e` | read the message waiting for you |
| `/q` | leave the room |

`/w` and `/who` also list the room, `/quit` also leaves, and `/mail` also reads your message.

### Squelch

`/sq 3` hides everything node 3 says, until you hang up or type `/sq 3` again. Joining, leaving, kicks and the room's own notices are never hidden, so a squelch cannot be used to miss what is going on. A squelch belongs to your call: it is dropped when you log off, and nobody inherits it when a new caller takes the node.

## Moderation

Staff in the room get four more commands:

| Command | What it does |
|---|---|
| `/k n [why]` | put node n back at the command prompt, with a reason |
| `/b handle` | bar a handle from the room, and remove them if they are in it |
| `/unb handle` | let them back |
| `/bans` | who is barred |

The room ban list is kept in the plugin's own folder and survives a reboot. It bars a handle from the chat room only: it is not a board ban, and it does not touch the IP ban list. Sixteen handles fit.

Staff can only act on callers below their own rank, the same rule the board uses everywhere else.

### Voting somebody out

When there are **no staff in the room** and **three or more callers** are, anybody can start a vote:

```
/vk 3
```

Two thirds of everyone but the target, rounded up, ends it. The window is sixty seconds. Every vote and every result is announced in the room and written to the log.

A vote can **only remove somebody from the room**. It cannot ban them, it cannot touch their account, and they can walk straight back in with `CHAT`. It is a way for a room to deal with a nuisance at three in the morning, not a court. Staff being present switches it off entirely, because then there is somebody who can actually deal with it.

## Messages

The message system is deliberately small: **one message per caller, waiting to be read**. It is a note left on the door, not a mailbox.

- `MAIL handle your message` at the command prompt, or `/email handle your message` in the room.
- Up to 512 characters, which is enough to say something real.
- The recipient is told `You have mail.` when they log in, when they enter the room, and straight away if they are already on.
- `MAIL` on its own, or `/e` in the room, reads the message and clears it.
- Sending to somebody who already has an unread message replaces it. The sender is told that is what happened, so nobody thinks two messages arrived.
- Messages need an account at both ends. Guests can neither send nor receive.
- The board holds 32 messages at a time. When they are all spoken for, a sender is told **the email system is full** and nothing is silently thrown away.
- A message that is not read within 14 days expires. The caller it was waiting for is told that a message expired, so they know they missed something.

### Mail is not private

Say it plainly: **this is not private messaging.** The messages are stored as plain text in the board's filesystem. Anybody holding the board can read them, anybody with the sysop password can download a backup that contains them, and the board is one chip on a shelf, not a service with a privacy policy. It is a convenience for leaving a note, and it should be treated exactly like a postcard pinned to a corkboard.

## Settings

Everything is in the `[plugin:chat]` section of `system.cfg`, and the sysop can edit it live with `CONFIG chat`.

```
[plugin:chat]
enabled = yes
read  = all         ; who may join and watch
write = all         ; who may talk
admin = sysop       ; who may clear the room
room  = Main        ; the room's name in the banner
rate  = 80          ; lines a minute one caller may send, 8 in a burst
history = 48        ; lines the room remembers

mail_slots = 32     ; messages the board holds at once, 0 switches mail off
mail_chars = 512    ; longest a message may be
mail_days  = 14     ; how long one waits before it expires

color_node    = cyan       ; the #2 in "#2:Daytona) hi"
color_punct   = darkgrey   ; the : and the rank bracket
color_handle  = ltgreen    ; the handle
color_text    = white      ; what was said
color_old     = darkgrey   ; history shown on the way in
color_notice  = yellow     ; *** joined, left, votes, kicks
color_room    = cyan       ; the banner, /s and the like
color_private = purple     ; a line meant for one caller
```

Colour names are the C64 palette: `black white red cyan purple green blue yellow orange brown ltred darkgrey grey ltgreen ltblue ltgrey`. A name the board does not know leaves that colour alone rather than blanking the screen.

`history` is claimed once when the plugin starts and given back when it stops. The default costs about 3 KB; a board with more memory can raise it as far as 2000 lines and hold a whole evening. If the board cannot spare what was asked for, it falls back to the default and says so in the log.

`CHATCLEAR` empties the room's memory. It needs the admin level.

## Turning it off

A board that is only a log viewer or a serial terminal does not need a chat room:

```
[plugin:chat]
enabled = no
```

The commands disappear from the menus with it.

## See also

- [COMMANDS.md](COMMANDS.md): every command on the board.
- [PLUGINS.md](PLUGINS.md): how plugins are configured and what they may use.
- [USERS.md](USERS.md): accounts, ranks and what the markers mean.
