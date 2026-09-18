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
 License:      GNU General Public License v2 or later
 SPDX-License-Identifier: GPL-2.0-or-later
 ===========================================================================
-->

# µnleashed BBS: announcing your board

A board nobody can find is a board nobody calls. The `announce` plugin sends a small heartbeat to a directory server every few minutes saying "I am here, this is my name, this is who runs me". The directory keeps a list of what is up right now.

It solves a second problem at the same time. The directory records the address the heartbeat arrived from, so a board on a home connection whose address changes stays findable without dynamic DNS. That is the cheapest DDNS there is, and it costs a couple of hundred bytes every ten minutes.

## The rules this plugin plays by

This is the only part of the system that talks to the outside world on its own, so it is held to a higher standard than the rest:

- **It is off until you switch it on.** No default board announces itself.
- **It never sends anything about callers.** Not handles, not addresses, not counts of who did what, not a single word anybody typed. The board's own name, who runs it, how to reach it, and how many of its lines are busy. That is the whole payload.
- **You can read it before you trust it.** `ANNOUNCE TEST` prints the exact bytes that would go out, and sends nothing.
- **The directory is a setting.** The project's own is the default because it has to be something, but the list is yours to change, the format below is public, and a board can post to several directories at once. A directory nobody can replace would contradict everything else in this repository.

## Switching it on

```
[plugin:announce]
enabled     = yes
name        = The Rusty Modem
owner       = KE9CXN
description = A BBS on a chip in a shack in Illinois
servers     = http://unleashedbbs.com/announce
host        =                     ; a DNS name of your own, if you have one
public_port = 6400                ; the port callers dial, if you forwarded a different one
interval    = 10                  ; minutes between heartbeats, 1 to 1440
token       =                     ; only if your directory issues them
```

| Key | What it is |
|---|---|
| `name` | what your board is called. Falls back to `hostname` if you leave it out |
| `owner` | you. A handle, a call sign, a name, whatever you want listed |
| `description` | one line, up to 120 characters, in your own words |
| `servers` | comma separated. Up to four directories, each `http://host[:port]/path` |
| `host` | the name you want listed. Leave it empty and the directory uses the address your heartbeat came from |
| `public_port` | the port on the outside. Set this if you forwarded an external port that is not 6400 |
| `interval` | minutes between heartbeats. Ten is plenty; a directory usually considers a board gone after three missed |
| `token` | a shared secret, if the directory you post to issues them |

`CONFIG announce` edits all of it from the board, and the plugin restarts with the new settings the moment you save.

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

{"software":"unleashed","version":"0.12.0",
 "name":"The Rusty Modem","owner":"KE9CXN",
 "description":"A BBS on a chip in a shack in Illinois",
 "host":"","port":2323,"nodes":6,"busy":0,
 "uptime":3600,"token":""}
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
| `token` | string | a shared secret, or empty |

A payload is around 200 bytes. Nothing in it identifies a caller.

### Response

Return `200` when the listing was accepted. Anything else is treated as a refusal and shown to the sysop as such. The body is ignored.

One optional header is understood:

```
X-Seen-Address: 203.0.113.9
```

The address the request arrived from. The board shows it under `ANNOUNCE` and on the `SYS` screen, which is how a sysop behind a changing address finds out what their address currently is. Sending it is good manners and costs a directory nothing.

### What a directory is expected to do

- Record the listing against the source address, or against `host` when the board supplied one.
- Treat a board as gone after a few missed heartbeats. Three intervals is the convention.
- Never publish anything the board did not send.
- If you issue tokens, refuse a listing whose token does not match the name it claims. That is the only thing stopping somebody listing a board as yours.

## Running your own directory

Please do. The server for unleashedbbs.com will be published under the same licence as the board, and it is a small program: accept a POST, validate it, write it to a list, serve the list. Nothing about this project should depend on one server staying up, including this part of it.

A board posting to several directories at once is an ordinary configuration, not a workaround:

```
servers = http://unleashedbbs.com/announce, http://bbs.example.org/announce
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
