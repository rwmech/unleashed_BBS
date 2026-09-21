<!--
µnleashed BBS: PLUGINS.md

Writing and running plugins: the API, the config section, access levels
and storage.

Copyright 2026 - Robert Mech
License: GNU General Public License v2 or later
SPDX-License-Identifier: GPL-2.0-or-later

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

Staff can see the state of every plugin with `PLUGINS`:

```
 Plugins                          1 of 8
 Name      Ver   State
 example   1.0   running
Disk free 612K, reserve 32K
```

## What ships

| Plugin | Purpose |
|---|---|
| `chat` | one chat room, DDial style (`#2:Daytona) hi`), room commands, moderation and the message system. On by default. Its own doc: [CHAT.md](CHAT.md). |
| `announce` | tells a directory server the board exists, so callers can find it, and learns the board's public address back. Off by default, sends nothing about callers. Its own doc: [ANNOUNCE.md](ANNOUNCE.md). |
| `serial` | shares a serial device: one operator with `write`, any number of watchers with `read` |
| `example` | the template, and what the tests drive |

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

Each one is optional.

| Hook | When |
|---|---|
| `start(bbs)` | after config load; return false to refuse |
| `stop()` | switched off, or a config reload |
| `tick(now)` | every 250 ms from the BBS loop; never block |
| `onConnect(s)` | a caller arrives, after terminal detection |
| `onLogin(s)` | a caller logs in |
| `onLogoff(s)` | a caller leaves |
| `onKey(s, key, now)` | only while the plugin owns that session |

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
- The core keeps 32 KB of free space in reserve so accounts can always be written. Once space is that tight, `plugins::path` returns false and the plugin should carry on without saving.

## Memory

The board has roughly 143 KB of free heap with a caller on, and 110 KB of that is one contiguous block. Keep plugin state static and small, declare what you need in the descriptor, and let the core refuse the plugin rather than run the board out of memory at 2am.
