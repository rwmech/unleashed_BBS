<!--
 ===========================================================================
  µnleashed BBS
  Electronic freedom on a microcontroller.
 ===========================================================================

 File:         ANNOUNCE.md
 Module:       Documentation / directory listing

 Purpose:      The announce plugin: how a board tells a directory it
               exists, exactly what it sends, and the protocol somebody
               needs to run a directory of their own.

 Audience:     Sysops switching it on, and anybody writing a directory
               server. The wire format section is the specification.

 Copyright 2026 - Robert Mech
 License:      GNU General Public License v3 or later
 SPDX-License-Identifier: GPL-3.0-or-later
 ===========================================================================
-->

# µnleashed BBS: announcing your board

A board nobody can find is a board nobody calls. The `announce` plugin sends a small heartbeat to a directory server every few minutes saying "I am here, this is my name, this is who runs me". The directory keeps a list of what is up right now.

It solves a second problem at the same time. The directory records the address the heartbeat arrived from, so a board on a home connection whose address changes stays findable without dynamic DNS. That is the cheapest DDNS there is, and it costs a couple of hundred bytes every ten minutes.

## The rules this plugin plays by

This is the only part of the system that talks to the outside world on its own, so it is held to a higher standard than the rest:

- **It is off until you switch it on.** No default board announces itself.
- **It never sends anything about callers.** Not handles, not addresses, not counts of who did what, not a single word anybody typed. The board's own name, who runs it, how to reach it, how many of its lines are busy, and a few facts about the board itself for the directory's badges. That is the whole payload.
- **You can read it before you trust it.** `ANNOUNCE TEST` prints the exact bytes that would go out, and sends nothing.
- **The directory is a setting.** The project's own is the default because it has to be something, but the list is yours to change, the format below is public, and a board can post to several directories at once. A directory nobody can replace would contradict everything else in this repository.

## Switching it on

```
[plugin:announce]
enabled     = yes
name        = The Rusty Modem
owner       = Daytona
description = A BBS on a chip in a shack in Illinois
servers     = http://unleashedbbs.net/announce
host        =                     ; a DNS name of your own, if you have one
public_port =                     ; what callers dial through the router; empty: the board's port
interval    = 10                  ; minutes between heartbeats, 1 to 1440
token       =                     ; left empty: the directory fills this in
share_activity = no               ; send call counts so a directory can rank
support     = lgbtq, literacy     ; causes you back, as slugs from the directory's /badges page
interests   = c64, electronics, ham ; what you are into, the same way
```

You do not have to edit the file. `CONFIG announce` puts every one of these
on a form, on a fresh board, with whatever the plugin is currently running
with already filled in.

| Key | What it is |
|---|---|
| `name` | what your board is called. Falls back to `hostname` if you leave it out |
| `owner` | you. A handle, a call sign, a name, whatever you want listed |
| `description` | one line, up to 120 characters, in your own words |
| `servers` | comma separated. Up to four directories, each `http://host[:port]/path`. The project's own directory answers on all three of its names; `.net` is the one meant for machines, and `.com` and `.org` are for people |
| `host` | the name you want listed. Leave it empty and the directory uses the address your heartbeat came from |
| `public_port` | **Outside** on the CONFIG page: the port callers dial from the internet, when your router forwards a different number to the board. Leave it empty if the router forwards the same number as the board's `port`, and the board sends that: the port it is listening on, which follows `port` from the restart that moves it |
| `interval` | minutes between heartbeats. Ten is plenty; a directory usually considers a board gone after three missed |
| `token` | leave it empty. The directory mints one on the first heartbeat and the board writes it back here itself |
| `share_activity` | `yes` adds counts of calls and caller-minutes over the last day, so a directory can rank by how busy a board is. Counts only, never who |
| `support` | causes you show support for, comma separated, as the slugs the directory publishes (unleashedbbs.com lists them at `/badges`). Up to 16, in at most 95 characters, which is the width of the CONFIG box |
| `interests` | what you are into, the same way: `c64, electronics, gardening`. Same limits |

`CONFIG announce` edits all of it from the board, and the plugin restarts with the new settings the moment you save.

### Badges

A directory can show a few badges beside a board's name. Four of them the board works out for itself and you cannot set, so they cannot be wrong about it:

- **system**: the chip, the flash this firmware image can use, and PSRAM when the firmware uses it, read at start: `ESP32 · 4 MB`, `ESP32-S3 · 16 MB · PSRAM`. The flash figure is the image's, not the chip's: a 16 MB module running the 4 MB image says 4 MB.
- **terminals**: what this firmware speaks, which is always `ansi`, `utf8`, `petscii` and `ascii`.
- **guests**: your `guest` setting.
- **features**: what works at the moment of the heartbeat, from `chat`, `mail`, `forums`, `files` and `camera`. Mail counts while chat runs with `mail_slots` above 0. Forums and files count while their plugin runs and a card is mounted, so `SD UNMOUNT` takes them off the next heartbeat. A card pulled without `SD UNMOUNT` is not noticed (nothing watches for it), so they stay until the board next starts without it.
  `camera` (1.1.0, camera boards only: the Freenove ESP32-WROVER CAM) is the directory's "This BBS can take pictures" badge. It counts while the camera plugin runs with a card mounted **and a sensor answered** its latest bring-up this boot. The board looks for the sensor once, at the first count of the photos after it starts (brought up and straight down again, no picture, no flash), and every snap looks again. So a board whose camera will not start never claims the badge, and one whose sensor stops answering drops it at the next snap. A reference ESP32 or S3 build never sends it.

`support` and `interests` are yours. The board tidies what you type before sending it: lower case, only `a-z`, `0-9` and `-` kept, a run of spaces, underscores or dashes inside an entry made one `-` and none left at either end (`" Mental Health "` goes as `mental-health`), empty entries and repeats dropped, anything longer than 24 characters dropped, and the first 16 kept. It does not check the words against a list: the directory does that, and ignores any it does not carry, so a typo is a missing badge rather than an error. Which list a slug belongs in is the directory's call too: unleashedbbs.com moved `ham` from support to interests and reads it in either. `ANNOUNCE TEST` shows exactly what went.

## Commands

| Command | What it does |
|---|---|
| `ANNOUNCE` | which directories, when each last answered, how many heartbeats have gone out, and the address the directory says you have |
| `ANNOUNCE TEST` | prints the exact payload and sends nothing |
| `ANNOUNCE NOW` | sends a heartbeat immediately instead of waiting |

Sysop only by default, like everything else that changes how the board presents itself.

## Why plain HTTP and not HTTPS

Because TLS would cost thirty to forty kilobytes of heap on a chip that has a few hundred, which is more than the rest of this plugin by an order of magnitude, and it would buy nothing worth having. Every field in the payload is public information: it is a listing, meant to be read by strangers. The one thing worth protecting is somebody claiming to be your board, and a token does that without a certificate store.

If you want it encrypted anyway, point `servers` at something on your own network and let that relay it.

## The wire format

This section is the specification. Anything that speaks it is a directory.

### Request

```
POST /announce HTTP/1.1
Host: <directory>
User-Agent: unleashed/<version>
Content-Type: application/json
Content-Length: <n>
Connection: close

{"software":"unleashed","version":"1.0.1",
 "name":"The Rusty Modem","owner":"Daytona",
 "description":"A BBS on a chip in a shack in Illinois",
 "host":"","port":2323,"nodes":11,"busy":0,
 "uptime":3600,"interval":10,"tz":-300,"token":"1935bc3c...",
 "system":"ESP32 · 4 MB",
 "terminals":["ansi","utf8","petscii","ascii"],
 "guests":true,
 "features":["chat","forums","files","mail"],
 "support":["lgbtq","literacy"],
 "interests":["c64","electronics","ham"]}
```

| Field | Type | Meaning |
|---|---|---|
| `software` | string | always `unleashed` from this firmware. A directory should accept others |
| `version` | string | the firmware version |
| `name` | string | the board's name, up to 40 characters |
| `owner` | string | who runs it, up to 40 characters |
| `description` | string | one line, up to 120 characters |
| `host` | string | the name to list, or empty to use the source address |
| `port` | number | the port callers should dial |
| `nodes` | number | how many caller lines the board has |
| `busy` | number | how many are in use right now |
| `uptime` | number | seconds since the board booted |
| `tz` | number | minutes east of UTC, daylight saving already applied. Lets a directory describe this board's busy hours in the hours its own callers keep, instead of in UTC. 0 when the clock has never been set |
| `interval` | number | minutes between heartbeats, so a directory knows when to call the board quiet rather than guessing |
| `token` | string | empty on the very first heartbeat, then whatever the directory issued |
| `calls24` | number | calls in the last 24 hours. Only when the sysop turned `share_activity` on |
| `minutes24` | number | caller-minutes in the last 24 hours. Same condition |
| `system` | string | the machine the board runs on: the chip and the flash its firmware image can use, `ESP32 · 4 MB` (UTF-8 middle dots), `host` from the host build. Never typed by anybody |
| `terminals` | array of strings | what the board can speak to a caller. Always `["ansi","utf8","petscii","ascii"]` from this firmware |
| `guests` | boolean | `true` if a caller can look around without an account: the board's `guest` setting |
| `features` | array of strings | what works right now, from `chat`, `forums`, `files`, `mail` and `camera`: forums, files and camera only with a card mounted, and camera only on a board whose camera sensor answered |
| `support` | array of strings | causes the sysop shows support for, as slugs from the directory's published list |
| `interests` | array of strings | what the sysop is into, the same way |

The last six are the badges, and every heartbeat carries all of them, empty lists included: a directory replaces them on each heartbeat, so a field left out is a badge taken down. A directory that does not know them ignores them.

A plain payload is around 450 bytes. The largest this firmware can build is 1,319: every text field at its longest with every character one that JSON has to escape, both lists full, and every number at its widest. The board's buffer holds 1,343, so a payload is never refused for size; if one ever were, the board would log it and send nothing rather than a cut-off half. Nothing in it identifies a caller, and nothing ever should.

### Response

Return `200` when the listing was accepted. Anything else is treated as a refusal and shown to the sysop as such.

```
HTTP/1.1 200 OK
X-Seen-Address: 203.0.113.9
X-Listing-Token: 1935bc3ca80c43fcd52bacf6d3db6673
X-Listing-State: pending
X-Listing-Public-In: 9840
```

| Header | What the board does with it |
|---|---|
| `X-Seen-Address` | the address the request came from. Shown under `ANNOUNCE` and on `SYS`, which is how a sysop behind a changing address finds out what theirs currently is. Sending it costs a directory nothing and is good manners |
| `X-Listing-Token` | the token for this listing. On the first heartbeat it is newly minted, and the board writes it into its own config so the listing survives a reboot |
| `X-Listing-State` | `pending`, `online`, `offline` or `queued` |
| `X-Listing-Public-In` | seconds until a pending listing appears, so a board can show `public in 2h41m` instead of nothing happening |

The same values appear in the JSON body, for implementations that would rather parse one thing than two.

### What a directory is expected to do

- Record the listing against the source address, or against `host` when the board supplied one.
- Mint a random token on the first heartbeat and return it. **Do not derive it from anything public.** A token computed from the board's name, or from its MAC address, is a lock whose key is printed on the door: the firmware and the reference server are both open source, so everybody has the algorithm.
- Treat an unknown token as a brand new listing. Never transfer an existing one.
- Hold a new listing back until it has sustained heartbeats for a few hours. This is the anti-spam measure that costs a spammer real infrastructure and a genuine board nothing, since it was going to be up anyway.
- Treat a board as quiet after three of its own intervals, rather than deleting it. A reboot should not cost a board the hours it spent earning its place.
- Limit automatic listings per source address, counted per `/64` on IPv6, and queue the rest for a human. Addresses are the scarce resource, which is why this is the control that bites.
- Never publish anything the board did not send, and never connect outwards to verify a listing: a directory that connects to whatever host and port a stranger posts to it is a port scanner with a public API.
- Treat activity figures as self-reported, because they are. If you rank by them, say so, and pair them with something you measured yourself, such as how long a board has been continuously up.

The token stops somebody taking over an existing listing. It is **not** a spam control and no token scheme could be, because tokens are free to mint.

## Running your own directory

Please do. The server behind unleashedbbs.com is published under the same licence as the board, at
[github.com/rwmech/unleashed_directory](https://github.com/rwmech/unleashed_directory): Python 3, standard library only, SQLite, one file. Accept a POST, validate it, write it to a list, serve the list. Nothing about this project should depend on one server staying up, including this part of it.

A board posting to several directories at once is an ordinary configuration, not a workaround:

```
servers = http://unleashedbbs.net/announce, http://bbs.example.org/announce
```

## The house rules on unleashedbbs.com

The default directory is a list of boards, run by the project. Its rules are short, because a list does not need many:

- **No hate.** Boards whose name or description attacks people for who they are do not get listed. This is not a judgement on what you run, it is a decision about what goes on our page.
- **Be honest about what you are.** The description should describe the board, not advertise something else.
- **It is public.** Assume everything you send is on a web page the moment you send it, because it is.
- **Get along.** That is the whole of it.

And the part worth saying out loud: **these rules bind us, not you.** Anybody can run a directory with different rules, or none, and the format above is published so they can. Taking a board off our list does not take it off the internet, and it was never meant to. We are only responsible for our own page.

## What the directory stores about you

The fields you sent, the address they arrived from, and when. It is a listing service, not an analytics product. Nothing is sold, nothing is shared, and delisting is a request away or simply switching the plugin off and waiting for the heartbeats to stop.

## See also

- [PLUGINS.md](PLUGINS.md): how plugins are configured and what they may use.
- [PUBLIC.md](PUBLIC.md): putting the board on the internet in the first place.
- [COMMANDS.md](COMMANDS.md): every command on the board.
