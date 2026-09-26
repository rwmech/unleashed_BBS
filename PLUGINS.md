<!--
µnleashed BBS: PLUGINS.md

Writing and running plugins: the API, the config section, access levels
and storage.

Copyright 2026 - Robert Mech
License: GNU General Public License v3 or later
SPDX-License-Identifier: GPL-3.0-or-later

Documentation for µnleashed BBS, part of the same distribution as the
source. See the LICENSE file for terms.
-->

# Plugins

A plugin that files anything under a caller's handle must implement `onRename(old, new)`, because a handle is a display name and can change. Chat learned this the hard way: its mailbox and its room ban list were both keyed by handle, so renaming somebody hid their own unread mail from them and walked them straight out of a ban, and neither failure said anything at all. The hook is called after `users.txt` has been written and only when that write succeeded.

A plugin is how a feature bolts onto the BBS: GPIO, a serial bridge, chat, a door. Plugins are compiled into the firmware and switched on in `system.cfg`, so a board only carries what it uses, and a new plugin arrives as new firmware (over the air, once OTA lands).

There is no dynamic loading. The ESP32 runs code straight from the firmware image, and anything loaded at run time would have to live in RAM we would rather give to callers.

## Turning one on

Every plugin reads one section of `system.cfg`:

```
[plugin:example]
enabled = yes
read    = all        ; who may look
write   = staff      ; who may change things
admin   = sysop      ; who may configure the plugin
greeting = howdy     ; the plugin's own keys
```

- Keys outside a section must appear above the first `[section]` line, or the core never sees them.
- The access ladder is `all`, `users`, `staff`, `co2`, `co1`, `sysop`, and `none`. `all` includes guests, `users` means an account, `staff` is any staff level, and `co1` means co-sysop 1 and up.
- Defaults when a line is missing: `read = all`, `write = staff`, `admin = sysop`.
- A plugin that isn't `enabled` never starts, and its commands don't exist.
- A plugin marked `PF_ON` in its descriptor is on without a section at all, and `enabled = no` turns it off. Chat and `sd` ship that way; everything else waits to be switched on.
- `PF_EARLY` starts a plugin before the ones without it. It exists for a plugin that provides something another plugin's requirements are checked against: `sd` mounts the card, and whether a `PF_SD` plugin may start is decided by whether a card is mounted. Leaving that to the order of the registry table would work and would be invisible, which is the kind of dependency that survives until somebody tidies the list alphabetically.
- `PF_FAST` (1.1.0) calls the plugin's `tick` every 20 ms (`BBS_PLUGIN_FAST_MS`) instead of every 250, for a plugin that draws something that moves: the lights, at fifty frames a second. A flag rather than a new hook, because a hook is a field appended to every descriptor in the tree and this is the same hook called more often. `tick` still must never block.

Staff can see the state of every plugin with `PLUGINS`:

```
 Plugins                          1 of 9
 Name      Ver   State
 example   1.0   running
Disk free 612K, reserve 32K
```

## What ships

| Plugin | Purpose |
|---|---|
| `chat` | one chat room, DDial style (`#2:Daytona) hi`), room commands, moderation and the message system. On by default. Its own doc: [CHAT.md](CHAT.md). |
| `announce` | tells a directory server the board exists, so callers can find it, and learns the board's public address back. Also sends the directory's badges: the chip and the flash its image can use (`plat::hardware`), the terminals, the guest setting, which of chat, mail, forums and files work (the last two only with a card mounted), and the sysop's `support` and `interests`. Off by default, sends nothing about callers. Its own doc: [ANNOUNCE.md](ANNOUNCE.md). |
| `serial` | shares a serial device: one operator with `write`, any number of watchers with `read` |
| `sd` | mounts an optional SD card over SPI and lets its `screens` folder override the stock screens, per file. On by default; costs one failed mount at boot on a board with no card. |
| `files` | publishes folders on the card as file areas, with XMODEM/YMODEM download and upload. Needs a card, so it does not start on a cardless board. |
| `forums` | topic message boards on the card: forums, subjects, replies. Needs a card; a sysop switches it on once the topic areas are set up. |
| `info` | the ten information pages a sysop writes (`INFO` / `I`, `/i` in the room). On by default, no card needed. |
| `lights` | two WS2812B outputs on the RMT peripheral (`plat::pixels*`): a drive light fed by `plat::diskPulse`, with PC, 1541, Disk II and breathing styles, and a strip of ten showing the caller lines, a Hayes front panel, several retro effects, or a colour and effect per pixel. Brightness is a percentage per output with a hard ceiling of 30. `PF_FAST`; off until switched on and given pins. `LIGHTS` shows what each output was last sent, which is also how the host tests read the pixels. |
| `example` | the template, and what the tests drive |
| `camera` | photos from the board's own camera (camera boards only, `BBS_HAS_CAMERA`): `SNAPSHOT` for a caller, a timelapse of its own, into the Photos and Timelapse file areas. Needs a card and a board wired for a sensor, so it neither starts nor exists in the binary on any other board. Its own doc: COMMANDS.md, `camera` under Plugins. |

## Writing one

A plugin is one static descriptor. Copy `src/plugins/example.cpp`, which exercises every part of the API, and add it to `src/plugins/registry.cpp`.

```c
extern const Plugin kExamplePlugin = {
    { "example", "Example plugin", "1.0", 0, 512, PF_CORE },
    start, stop, tick,
    nullptr, nullptr, nullptr,      // onConnect, onLogin, onLogoff
    onKey,
    kCommands, sizeof(kCommands) / sizeof(kCommands[0]),
};
```

The descriptor says what the plugin needs: heap while running, bytes of storage, and flags. At boot the core checks those against the free heap and free space and refuses a plugin that doesn't fit, with a line in the log and the reason in `PLUGINS`.

### Hooks

Each one is optional; leave it null and the core skips it. This is the full
list, in the order `Plugin` declares them, because the struct is filled
positionally: a field inserted anywhere but the end silently shifts every
one after it. A descriptor written before a hook existed still compiles and
simply offers none of it, which is why new hooks are always appended, never
inserted.

| Hook | When |
|---|---|
| `start(bbs)` | after config load; return false to refuse |
| `stop()` | switched off, or a config reload. Since 1.1.2 a `CONFIG` save stops and starts only the plugin whose section it wrote and any other whose section of `system.cfg` is not what it started on (every plugin for a core page, and every one when `sd`'s section changes, since the others wait on the card), so a plugin must not count on seeing a stop at every save |
| `tick(now)` | every 250 ms from the BBS loop, every 20 ms for a `PF_FAST` plugin; never block |
| `onConnect(s)` | a caller arrives, after terminal detection |
| `onLogin(s)` | a caller logs in |
| `onLogoff(s)` | a caller leaves |
| `onKey(s, key, now)` | only while the plugin owns that session |
| `status()` | one short line for the dashboard (`DASH`, the plugins section on its second page, and the 132 column page's Directory and Lights rows), or null for none; return a pointer to storage that outlives the call, and do no real work: `DASH n` redraws on a timer, and a frame opens no file. Start it with the plugin's own name and a colon (`SD:`, `Files:`), which is how the 132 column page splits it into a value and a note |
| `setting(key, out, n)` | CONFIG wants this plugin's live value for one of its declared `settings` keys, used when `system.cfg` does not carry it yet; leave `out` empty for a key you do not recognise, so a blank on the form never quietly means something else |
| `rows(s)` | once per line after `Bbs::startPluginList`, with the row number in `Session::listIdx`, until it returns false; the plugin gets the core's paging, `[More]` prompt, abort keys and output backpressure instead of reimplementing them. A row whose data is not ready yet (being read on the runner, below) calls `b.listHold(s)` and returns true: the core draws nothing, keeps the row number, and asks again next pass (1.1.2) |
| `onPresence(s)` | what the outside can see about who is on has changed: `SHOW`, `HIDE` or `LURK`, not only `onLogin`/`onLogoff`, because `Bbs::publicBusy` counts a session only while it is visible |
| `onBytes(s, b, n, now)` | raw input as it arrived, only while the plugin owns the session and has turned on `Bbs::setRawInput`; telnet has already been unescaped, so `IAC IAC` is one 0xFF here |
| `onRename(old, new)` | a caller's handle changed; called after `users.txt` has been written and only when the write succeeded, so a plugin acting on it can trust the new name |
| `listDone(s, aborted)` | a paged list this plugin started with `startPluginList` has finished; `aborted` is true when the caller stopped it at `[More]` rather than reading to the end. The core deliberately draws no prompt for a plugin-owned session, so this is the plugin's only signal to put something on the screen |
| `liftInput(s)` | 1.1.0. A notice (a page, a broadcast, `SHUTDOWN`'s countdown, "you have mail", an arrival, a ring for the sysop) is about to be printed to a caller this plugin owns: take the input line out of the way and leave the cursor at column 0 of an empty line. Return false for "not now" and the notice waits for a later pass. Only called while the session is `SState::Plugin`, owned by this plugin, not in raw mode, with nothing waiting to be sent. Leave it null, as the serial bridge does, and notices wait for the main prompt as they always did |
| `restoreInput(s)` | 1.1.0. The notice is out: put the prompt back, with what the caller had typed on it. Called once for each `liftInput` that returned true, possibly much later, because a ring for the sysop is a one-key question the core asks between the two. If `s.ed` is not active when it arrives, the core used the line editor in between (a caller ringing from the chat room), so start a fresh line rather than redrawing the old one |
| `waiting(s, out, n)` | 1.1.0. What this plugin has waiting on the staff member `s`, for the dashboard's "Waiting on you" row: write one short phrase into `out` and return true, or return false for nothing. The core joins the answers in plugin order, after the sysop's ring notes. `s` is the staff member looking, so answer for their level, and in fewer words under 60 columns (`s.term.cols()`): the file areas say `2 uploads to approve` or `2 uploads`, chat `3 unread mail` or `3 mail`. Called once per dashboard frame, so the same contract as `status()`: a figure the plugin already keeps, no file opened, nothing counted |
| `pinShares(key, otherPlugin, otherKey, onPage)` | 1.1.0, camera boards only, appended after `waiting`. `CONFIG` refuses a `PS_PIN` row a switched-on plugin already holds; this is the one way round that, asked of both plugins in turn. Whether this plugin's `PS_PIN` row `key` may share its GPIO with `otherPlugin`'s row `otherKey`. `onPage` gives another key's value as the CONFIG page being saved currently has it, or null when that page belongs to a different plugin, so the plugin answers from what it is actually running with instead. The camera's flash pin shares with the lights plugin's drive pin only in `pixel` mode, where the flash *is* that pixel; in `pin` mode nothing shares |

### Settings

`settings` and `settingCount` are a table, not a hook: the rows CONFIG offers
on this plugin's page, in the order they should appear. Declaring one is what
makes it editable at all; a key CONFIG has never heard of is invisible until
somebody edits `system.cfg` by hand.

```c
const PluginSetting kSettings[] = {
    { "greeting", "Greeting", PS_TEXT, 0, 0, 40 },   // key, label (9 chars), kind, lo, hi, cap
    { "port", "Outside", PS_OPTNUM, 1, 65535, 5,     // an optional seventh: the row's note
      "What callers dial through your router." },
};
```

The seventh field, `note`, is optional and appended (1.1.0): the line CONFIG
shows on the form's status line while that row has the focus, 38 characters
at most. Leave it out and the row shows the usual movement hint.

The ninth and tenth, `wide` and `wideNote`, are the label and the note at 80
columns (1.1.0): 20 and 78 characters. A form is the 40 column card on a
terminal under 80 and a wider layout at 80 and up, plain ASCII included, and
each row picks its words for the width. Either may be left null, and the row
then shows its short one, padded. The fields are filled positionally and C++17
has no designated initialisers, so a row that sets `wide` spells the two
before it:

```c
{ "cs", "CS pin", PS_PIN, 0, 33, 2, nullptr, nullptr, "Chip select GPIO" },
```

CONFIG holds a value of up to `kSettingMax` characters (120, `plugin.h`); a
`cap` above that is cut to it on the form. A plugin whose values can be longer
should `static_assert` against it, as announce does for its description.

The eleventh and twelfth, `warnAbove` and `warn` (1.1.0), are for a `PS_NUM`
that CONFIG takes up to `hi` but asks about past a level. Saving a changed
value above `warnAbove` puts one question to the sysop for the whole page,
naming every such row with the first one's `warn` after it:
`Strip % over 30: ten pixels can draw more than USB gives. Save anyway? (y/N)`
at 80 columns, the names alone inside 38 at 40. Y saves; anything else leaves
the page open with nothing saved, and plain ASCII asks the row again. `warn`
null, as every row but two ships, means the row never asks. The lights'
brightness rows are the example:

```c
{ "strip_bright", "Strip %", PS_NUM, 1, 100, 3, "White at 10 is 60 mA; at 30, 180 mA.", nullptr,
  "Strip brightness %", "Ten pixels in white: 60 mA at 10, 180 mA at 30, 600 mA at 100. Over 30 asks.",
  30, "ten pixels can draw more than USB gives." },
```

The question comes from `Form::ask`, which any form owner can use: it asks on
the status line (or on a line of its own in plain ASCII) and the next Save is
the answer, with `Form::takeConfirmed()` true once.

`kind` is `PS_TEXT`, `PS_NUM`, `PS_YESNO`, `PS_INFO`, `PS_PIN` or `PS_OPTNUM`.
`PS_OPTNUM` is a `PS_NUM` that may also be saved empty, written as an empty
value, which the plugin reads as its own default; `setting()` should return
empty while the file does not set it, so the form shows the blank that means
"default". It is its own kind because an empty value parses as 0, and on a
`PS_NUM` whose range starts at 0 that would quietly mean something. `PS_PIN` is a
GPIO number: numeric like `PS_NUM`, with -1 meaning none, and CONFIG refuses
pins 6 to 11 because on the WROOM they are wired to the flash chip. `PS_INFO` is shown but
never editable and never written back, for a value the plugin does not own:
announce's `name` setting shows the board's name, which belongs to
`board_name` on the core, rather than opening a second editable field that
could drift from it. `setting()` supplies the running value for each key so a
blank on the form means "not set", not "I cannot tell you".

Three more, from the lights plugin (1.1.0):

```c
{ "drive_pin", "Drive pin", PS_PIN,  -1, 33, 2, "The disk light: one pixel. -1 is off." },
{ "strip_fx",  "Strip",     PS_CYCLE, 0,  0, 7, "nodes: one pixel for each caller line.",
  "nodes|hayes|blinken|scanner|c64|boing|vu|rainbow|manual|off" },
{ "led",       "Pixels",    PS_PAGE,  0,  0, 0, "Manual: each pixel its own effect." },
```

- `lo` is signed. A `PS_PIN` whose range starts at -1 takes -1, and it means
  the plugin's "off"; one whose range starts at 0 refuses it, because a
  plugin such as `sd` has no "off" for a pin and its parser would decline a
  -1 CONFIG had written. The range says which, not the kind.
- `CONFIG` refuses two `PS_PIN` rows on one page holding the same pin, on the
  later row: "That is the drive pin. Pick another."
- And a pin anything else on the board holds (1.1.0): BOOT (GPIO 0), the
  console's pins, the core's LED and backup button, and every `PS_PIN` row of
  every other plugin compiled in that is switched on, read from the file or,
  where it has no line, from that plugin's `setting()`. Declaring a pin as
  `PS_PIN` is all a plugin does to be covered, in both directions: its pins
  are checked, and nothing else may take them while it is on. So give
  `setting()` an answer for every pin, or a pin left at its default is one
  CONFIG cannot see.
- `PS_CYCLE` steps through `choices`, the eighth field, bar separated, the way
  a level does: Space for the next, a letter for the first word starting with
  it, the same letter again for the next such word. CONFIG refuses a value
  that is not one of the words, since plain ASCII line mode types into the
  same buffer.
- `PS_PAGE` is a button to a page of its own, holding every setting whose key
  is this one's followed by a number or an underscore (`led` holds `led1` to
  `led10`, chat's `color` holds `color_node` to `color_action`); those rows
  are left off the plugin's main page. For a plugin with more rows than a
  form holds: sixteen, less the four the core puts first. The button's text is
  the plugin's `setting()` for the key. Escape on the page, or saving it, comes
  back to the main page, and the page will not open over unsaved changes.
- `PS_GROW` is a `PS_PAGE` whose rows are slots, used or not (1.1.0). While
  they fit on the main page they are shown there instead of the button: the
  rows that have a value, in the file or from `setting()`, and the first that
  has none, so the page grows a row at a time. The forums' sixteen topics
  work this way, twelve in place and then the Topics button. It is its own
  kind because an empty row means "unused" only for a slot: chat's colours
  are a plain `PS_PAGE`, since an empty colour means the default and showing
  only the first empty one would hide the rest. One `PS_GROW` a plugin: a
  second would need `groupInline` to count its rows.
- A row on a `PS_PAGE` page can itself be a packed composite with a sub-page
  (the lights' pixels are `kLedParts` in `bbs_sysop.cpp`: Effect and Colour,
  both cycles). A composite part may be `CK_CYCLE` with a `choices` list.

### Commands

Plugin commands use the same table as the core, and HELP is generated from it:

```c
{ "PING", "", 0, CF_READ, "PING", "example: say hello",
  [](Bbs& b, Session& s, const char* arg, uint32_t now) { ... } },
```

Tag every command `CF_READ`, `CF_WRITE` or `CF_ADMIN`. An untagged command counts as write, so a careless plugin fails shut. A caller who doesn't hold the level never sees the command in HELP and gets "unknown command" if they type it.

### Owning a session

A serial bridge, a chat room or a door takes over the caller's screen:

```c
if (!b.own(s, myIndex)) return;    // keys now come to onKey
...
b.release(s);                      // back to the command prompt
```

While a plugin owns a session the idle timeout pauses, because watching is not idling. The per-call and per-day time limits still apply, and staff with `NOLIMITS` are exempt as always. Output goes through the same terminal layer as the rest of the BBS, so PETSCII, ANSI and plain ASCII callers all work.

A plugin that owns sessions should offer `liftInput` and `restoreInput`, or nobody inside it hears a page, a broadcast or `SHUTDOWN`'s warnings until they leave. The pattern the shipped plugins use: `liftInput` ends the line the caller is on (the chat room erases its input line instead, marker and all), and `restoreInput` draws the prompt or question they were at again, with `s.ed.redraw` rather than `s.ed.begin` so what they had typed survives. Return false from `liftInput` while anything binary is on the line.

The core may also take a session away from its plugin: a sysop answering a ring from inside it goes to the chat room. It is let go the way a dropped line lets it go, with no hook called and every claim the node held released, so a plugin's own per-session state has to be reset on the way back in, never trusted from the last visit.

### Slow work stays off the loop

The BBS loop is cooperative: a `tick` or a command handler that blocks stalls every caller on the board, not only the one that asked for it. Anything that can take more than a few milliseconds (a directory walk, a file read a row, a name lookup, a JPEG, a whole-file rewrite) goes on **the background runner** (`src/core/runner.h`, 1.1.2), one task shared by the whole board: pinned to the BBS task's core, three priorities below it, so it only runs in the loop's idle time, started when a job is posted and gone three seconds after the last one. Its stack is `BBS_RUNNER_STACK` (8 KB, from the heap), and `SYS` shows its lowest free and its longest job.

```c
runner::Job g_job;                           // static: it outlives the caller who asked
void work(runner::Job&) { ...slow... }      // on the runner: no Session, no Term, no Timeline

g_job.work = work;  g_job.name = "my job";
runner::post(g_job);                         // loop only; false when the queue (8) is full
...
if (runner::done(g_job)) { ...read results...; runner::collect(g_job); }   // in tick
```

- A job is posted and collected from the loop only. Its state moves `IDLE -> QUEUED -> RUNNING -> DONE`, set last by the runner once everything the job wrote is written, so the loop reads results only after `done()`.
- Results go in the job's own struct, never into a `Session`. A caller can hang up while a job runs and the session be handed to somebody else, so a job that answers a caller records `Session::call` (a serial number every connection gets) and the node, and the loop checks both before drawing anything.
- Jobs run one at a time, in order. A job that loops over many files calls `runner::breathe()` between them, so the loop gets the core back.
- The caller who is waiting sees a spinner (`Bbs::startWait`, or the plugin's own), never a frozen line.
- Two things stay on the loop: anything that has to be in step with the callers, such as renaming a screen file a caller may be reading (the loop closes theirs first), and anything that draws.

The camera plugin (`BBS_HAS_CAMERA`) was the first job too slow for a tick and ran its own task until 1.1.2; it is a runner job now, unchanged, with its phases (`Idle -> Working -> Ready -> Go -> Exposed -> Writing -> Done`) moved by `tick`. One snapshot runs at a time on the whole board.

A plugin that edits a file another plugin owns asks that plugin rather than writing it: the camera asks the files plugin to write a photo's description (`files::photoDesc`, `files::photoTidy`), which queues the edit and does it on the runner, so each `FILES.BBS` has one writer (1.1.2).

### Talking to callers

| Call | What it does |
|---|---|
| `bbs.sayTo(s, color, text)` | one line on a caller's screen, then redraw what they were at |
| `bbs.eachSession(fn, ctx)` | walk every session |
| `bbs.setDoing(s, "CHAT")` | what staff see in the Doing column of WHO and DASH |
| `bbs.prompt(s)` | end a command back at the prompt |
| `s.ownerData` | a 32-bit scratch word per session, yours while you own it |

### Config and storage

```c
plugins::forEachKey(myIndex, readKey, nullptr);    // your own keys
plugins::path(myIndex, "count", buf, sizeof(buf)); // <fs>/p/<name>/count
```

- Each plugin gets its own folder and may not touch core files.
- Only plugins shipped in this repository (`PF_CORE`) get storage at all.
- `PF_SD` says a plugin's files live on the SD card. It gets `<sd>/p/<name>/` instead of `<userdata>/p/<name>/`, and it does not start at all when no card is mounted (`PLUGINS` says "no SD card"). There is deliberately no fallback to internal flash: a plugin that quietly writes somewhere other than where it said it would is worse than one that is refused, because the sysop pulls the card expecting the data to be on it.
- The free-space check follows the same split. A `PF_SD` plugin's `storageBytes` is weighed against the card, not against the 608 KB flash partition it is never going to touch.
- The core keeps 32 KB of free space in reserve so accounts can always be written. Once space is that tight, `plugins::path` returns false and the plugin should carry on without saving. The check reads the kept free-space figure (1.1.2, `src/core/space.h`), measured on the runner at boot and at each staff login, so a write never walks the partition to find out; a plugin that writes a lot at once can call `space::stale` for its partition so the next staff login measures it again.
- `plugins::readPath` is for reads: it builds the same path without the free-space check.

## Memory

Memory is the tight resource. On the reference WROOM-32E, static RAM is within a few KB of the linker's ceiling (180,736 bytes), and the free heap is whatever the radio, lwIP and the card driver leave, which `MEM` and `SYS` report on a running board. Measure there rather than trusting a figure written here. Keep plugin state static and small, declare what you need in the descriptor, and let the core refuse the plugin rather than run the board out of memory at 2am.
