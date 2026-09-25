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
 License:      GNU General Public License v3 or later
 SPDX-License-Identifier: GPL-3.0-or-later
 ===========================================================================
-->

# µnleashed BBS: chat and messages

The chat room is one room, the way DDial and Gtalk did it. Everybody who joins sees every line as it is said, each line carries who said it and which node they are on, and nothing scrolls past that a caller has not seen.

`CHAT` joins. `/q` leaves, and `/q+` leaves and logs off in one go. That is the whole thing.

## What a line looks like

```
#2:Daytona) evening all
```

- `#2` is the node. The sysop's hidden node shows as `#S`.
- `:` and the bracket are punctuation, in their own colour.
- `Daytona` is the handle.
- The bracket says the rank: `)` a caller, `*` a guest, `>` a co-sysop, `]` the sysop.

Room notices (`*** #2:Daytona) joined`) are in their own colour and are never hidden by anything, and a bell rings for everyone currently watching the room when one is a join (unless they have `/b` off). A private line arrives marked `P`, not `>`, because `>` is already the co-sysop bracket, and it rings your bell too.

## Typing

There is no prompt character. The cursor sits at the start of the line, and what you type stays where you put it: **lines said by other people while you are part way through yours are held until you press Enter**. That is per caller, not for the room. Everyone else sees the room live while you type.

Nothing is dropped. The room keeps a buffer (48 lines by default) and a caller who joins sees the last few lines of it, dimmed. If somebody types for long enough that the held lines are close to filling the buffer, their own line is lifted off the screen, the room prints underneath, and their line is put back so they can carry on.

Callers are rate limited: eight lines in a burst, then 80 lines a minute, which is faster than anyone types. Only the caller who trips it is told; the room never sees the message.

## Room commands

A line that starts with `/` is a command, not something you said.

| Command | What it does |
|---|---|
| `/?` | this list, with the staff lines when you are staff |
| `/? cmd` | one command in full, the same text `HELP cmd` gives at the main prompt. `/? staff` shows the moderation commands to staff |
| `/s` | who is in the room, with any away notes |
| `/p n text` | one line to node n only |
| `/p n*` | **stick** the conversation to node n: every line you type goes to them alone, and the input line shows `[>n]` so you always know. `/p*` ends it. From DDial, where it existed because retyping `/p 3 ` in front of every line is nine keystrokes on a C64 |
| `/sh [n]` | replay the last n lines the room said, 20 by default |
| `/whois handle` | a caller's profile, the same public fields `WHOIS` shows at the main prompt. Email, address and phone stay hidden unless it is your own account or you hold `USERS` |
| `/page n why` | page node n, as distinct from talking to them. A private line is part of a conversation; a page is "look at your screen". It reaches them wherever they are, the same as `PAGE` at the main prompt: in the room, part way through a line, it lifts their line, prints, and puts it back |
| `/o [reason]` | ring for the sysop, the same as `OPERATOR` at the main prompt: `/o` alone asks what for. If the sysop answers, you are talking to them in the room with `[>S]` on your line. See "Ringing for the sysop" below |
| `/i [n]` | the information pages: `/i` lists them, `/in` reads page n. `/in-` clears page n for whoever may write them |
| `/codes` | the colour and effect codes you can put in a message, the same list `CODES` shows at the main prompt |
| `/b` | on its own, bell on or off for this call: pages, broadcasts, somebody joining or logging in, a private line, a ring for the sysop, `@BELL@` in a message. `/b handle` (with a handle) is the staff bar instead |
| `/me text` | an action line, printed as `** Handle text **` with no node tag or bracket, because it is prose about you rather than something you said. Five every 30 seconds; past that, say it instead |
| `/a [note]` | away with a note, or back again when the note is left off |
| `/sq n` | hide node n's lines until you leave the room or type `/sq n` again |
| `/t` | the time, and how long you have left this call |
| `/clear` | wipe your screen and start fresh |
| `/email h text` | leave a message for a caller to read later |
| `/e` | read the oldest message waiting for you, inline, the same as `/whois` and `/i`: no need to leave the room for one message. Reading gives the same `[R]eply [S]ave [D]elete` choice `MAIL` does; the full mailbox is `MAIL` at the main prompt |
| `/q` | leave the room |
| `/q+` | leave the room and log off, the same send-off as `BYE`. The room sees `*** you logged off` |

`/w` and `/who` also list the room, `/quit` also leaves, `/quit+` also logs off, `/mail` also reads your message, `/history` also replays, `/bell` always means the bell, `/wi` is short for `/whois`, and `/operator` is `/o`.

### Notices in the room

A page, a broadcast, `SHUTDOWN`'s countdown, "You have mail" and somebody logging on all reach you in the room (1.1.0). If you are part way through a line it is lifted, the notice prints with its bell and tag, and your line, `[>n]` marker and all, comes back underneath. `/b` stops the bells, not the lines.

### Ringing for the sysop

`/o can't find the drop box` rings the sysop, exactly as `OPERATOR` does at the main prompt: the same limits (one ring every 3 minutes, three a call, one at a time on the board), the same answers, and what you wrote left in the sysop's MAIL when nobody answers (a note, when the board has no sysop account or mail is off). While it rings the spinner is on your line and any key stops it.

If the sysop answers, you stay in the room with a sticky private aimed at the sysop, `[>S]` on your input line, so what you type goes to them alone. `/p*` puts you back to talking to the room.

**For the sysop**, a ring that arrives in the room is two lines in the room's voice and no question, because the keys in the room are the room's:

```
--> quantumrob (3) is ringing: can't find the drop box
--> /o answers, /o- declines.
```

`/o` answers: the caller is brought into the room if they rang from the main prompt, and the two of you are stuck to each other, `[>3]` on your line. `/o-` declines, and they are told. Anywhere else on the board the sysop gets a one-key question instead; see COMMANDS.md, "Ringing for the sysop".

### Sticky private conversations

`/p3*` sends everything you type to node 3 until you stop. The input line carries `[>3]` the whole time, because the only real risk here is forgetting you are in it and saying something for one person that you meant for the room. The sysop's node is `S`: `/pS*`, and `[>S]` on the line.

A line said in the room while your line is empty prints above the marker, and the marker comes back under it (1.1.0: it used to print after the marker, and a re-armed line could carry the marker twice).

If they leave while you are stuck to them, the mode ends and **the line you were typing is not sent anywhere**. Falling back to the room would be precisely the accident the marker exists to prevent.

A stuck line is the same code as a typed `/p`: the same `P` marker on their screen, the same away note back to you, the same rate limit, the same confirmation. Two send paths is how one of them ends up not checking something.

### Squelch

`/sq 3` hides everything node 3 says, until you leave the room or type `/sq 3` again. Joining, leaving, kicks and the room's own notices are never hidden, so a squelch cannot be used to miss what is going on. A squelch belongs to this visit to the room, not the whole call: walking back in with `CHAT` clears it, and of course logging off does too. Nobody inherits a squelch when a new caller takes the node.

## Moderation

Staff in the room get five more commands:

| Command | What it does |
|---|---|
| `/k n [why]` | put node n back at the command prompt, with a reason |
| `/t n +m` | give node n m more minutes, or `-m` to take them away. The same convention as `TIME n +/-m` at the main prompt, and the same command underneath, so a caller's time warnings re-arm exactly as they do there |
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

The message system is small, but **nothing you are sent is ever thrown away
to make room for something else**. It is a small mailbox, not a note left on
the door.

- `MAIL handle your message` at the command prompt, or `/email handle your message` in the room, for a short one on a single line. `MAIL handle` with nothing after it opens the same message editor a forum post uses, for something longer.
- Up to 512 characters over 16 lines, which is enough to say something real.
- The recipient is told `You have mail.` when they log in, when they enter the room, and straight away if they are already on.
- **`MAIL` on its own is a place**, the way `FILES` and `FORUMS` are: a numbered list with `*` marking what is new. A number reads that message, Enter reads the oldest new one, `W` writes to somebody, `?` the keys, `Q` or ESC leaves. `/e` in the room reads the oldest new message inline, without leaving the room, for when one message is all you want.
- Reading shows a header (`#n of m`, who it is from, when), the body with its `@-codes` acted on, and `--> EOM <--`. **Reading it does not dispose of it**: the board then asks `[R]eply  [S]ave  [D]elete`, plus `Enter` for the next new message and `Q` back to the list when you got there through `MAIL`. Nothing is touched until you answer.
- **`R` replies**, opening the same message editor a fresh `MAIL handle` does, and the message you answered goes with it. That is one action rather than two on purpose: doing it in two steps would mean either deleting before the reply is stored, which loses the original if your reply is refused, or sending first and leaving you answering the same message again if the delete fails. If the reply cannot be stored, nothing moved and the original is still there.
- **`S` keeps it.** It stays in your box and can be read again, but it stops ringing `You have mail`, because something you decided to keep is not news. It still counts against your limit: it is still taking up room.
- **`D` deletes it**, and that is the only thing that does.
- ESC, or Enter, leaves the message unread and changes nothing. Any other key is ignored rather than guessed at, because two of the three choices cannot be undone.
- **How many you can have waiting depends on where the mail lives.** Three on a board with no SD card, because that storage is shared with the accounts and is the thing that has to survive. Twelve with a card, which has room and no reason to ration.
- **A full mailbox is refused, never emptied.** The sender is told the box is full and that nothing was replaced, which is something they can act on: wait, or reach the person another way.
- Messages need an account at both ends. Guests can neither send nor receive. The one exception is a ring for the sysop that nobody answered (1.1.0): the board leaves it in the MAIL of the sysop's account (the one `CONFIG board` names, or the last to elevate), from the caller, a guest's handle marked `*`, first line `Ring:` and the reason. It follows the same rules, box limits included; see COMMANDS.md, "Missed rings go to MAIL".
- The board holds 64 messages in total. When they are all spoken for a sender is told the board's mail is full.
- A message that is not read within 14 days expires. The caller it was waiting for is told that a message expired, so they know they missed something.

> An earlier version held one message per person and let a new one replace an
> unread one, telling the sender it had done so. That meant a third party
> writing to you could destroy a message you had not read yet. It was a bad
> design and it is gone.

### Mail is not private

Say it plainly: **this is not private messaging.** The messages are stored as plain text in the board's filesystem. Anybody holding the board can read them, anybody with the sysop password can download a backup that contains them, and the board is one chip on a shelf, not a service with a privacy policy. It is a convenience for leaving a note, and it should be treated exactly like a postcard pinned to a corkboard.

## Settings

Everything is in the `[plugin:chat]` section of `system.cfg`, and the sysop can edit it live with `CONFIG chat`. Every setting below is on that page with the value the room is running with, whether or not the file has a line for it (1.1.0); the eleven colours are on a page of their own behind the Colours button, each stepped through the colour names with Space or picked by its first letter.

```
[plugin:chat]
enabled = yes
read  = all         ; who may join and watch
write = all         ; who may talk
admin = sysop       ; who may clear the room
room  = Main        ; the room's name in the banner
rate  = 80          ; lines a minute one caller may send, 8 in a burst
history = 48        ; lines the room remembers

mail_slots = 64     ; messages the board holds at once, 0 switches mail off
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
color_marker  = cyan       ; the --> in front of anything the board says
```

## The board's own voice

Anything the board says in the room is marked `-->`:

```
--> Main: 3 here. /s who, /q quits.
--> No such command. /? for the list.
--> Mon 21 Sep 09:13
```

The room has no prompt character, DDial style, so without a marker a line
from the board is indistinguishable from somebody typing the same words.
`-->` costs four columns and removes the ambiguity entirely.

It is deliberately **not** on everything:

- `*** Daytona joined` and the other `***` notices keep their own mark. They
  are events, not answers, and they already read as such.
- The welcome screen is artwork, not the board talking.
- The room command list gets the marker on its heading only. An arrow on all
  sixteen rows turns a table into a wall.

`color_marker` themes it like every other part of a room line.

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
