<!--
 ===========================================================================
  µnleashed BBS
  Electronic freedom on a microcontroller.
 ===========================================================================

 File:         PUBLIC.md
 Module:       Documentation / going public

 Purpose:      How to put a board on the public internet, what that
               actually exposes, and the two settings that deal with the
               risks that are real.

 Audience:     Anybody about to forward a port. Read the risk section
               before you do, not after.

 Copyright 2026 - Robert Mech
 License:      GNU General Public License v3 or later
 SPDX-License-Identifier: GPL-3.0-or-later
 ===========================================================================
-->

# Putting your board on the internet

People think running a public system is something you need a provider, a bill and a certificate for. It is not, and it never was. A board on the internet is a machine that answers a phone number. The phone number is now an address and a port, and forwarding it is one line in your router.

## What you need first

Two things, and one of them catches most people out.

**A public address that people can reach.** Some internet providers put home connections behind carrier-grade NAT, which means your router does not have a public address at all and no amount of port forwarding will help. Check it: compare the address your router says its internet side has against what a "what is my IP" page tells you. If they are different, you are behind your provider's NAT, and you need to ask them for a real address, pay for a static one, or use a tunnel.

**A name, because your address will change.** This is the part worth understanding before you start. Almost every home connection gets its address by DHCP from the provider, on a lease, and that address changes: on a reboot, on a line fault, on an outage, or just because the lease expired. A board people reach at `198.51.100.23` today is a board nobody can reach next Tuesday, and your callers have no way of knowing where it went. A number that changes is no use to anybody.

So get a dynamic DNS name before you tell a single person about the board. A small client (usually built into your router) tells the provider your current address whenever it changes, and the name keeps working. Callers dial `yourboard.example.net 6400` and never think about it again.

| Provider | Notes |
|---|---|
| [No-IP](https://www.noip.com/personal) | one free hostname, no card. The catch: you have to reconfirm it every 30 days from an emailed link or it is released |
| [Dynu](https://www.dynu.com/en-US/DynamicDNS) | free, and accounts do not expire |
| [FreeDNS](https://freedns.afraid.org/) | free, long running, a lot of shared domains to pick from |
| [Cloudflare](https://developers.cloudflare.com/dns/manage-dns-records/how-to/managing-dynamic-ip-addresses/) | if you already own a domain there, update a record from a script or ddclient |

Most routers have a dynamic DNS client built in, under a name like DDNS or Dynamic DNS, and it takes about two minutes. If yours does not, run a client on any machine that is always on.

Paying your provider for a static address also works, and some of them will sell you one for a few pounds or dollars a month. That is the tidiest answer if it is on offer.

DuckDNS used to be the obvious free recommendation. It has been offline since August 2025, so do not build anything on it.

## The whole procedure

1. Get a dynamic DNS name, or a static address from your provider.
2. Give the board a fixed address on your own network.
3. Forward one TCP port on your router to that address, port 6400.
4. Check it from outside.
5. Tell people it exists.

Nothing below is longer than it is because it is difficult; it is longer because the parts that carry real risk deserve saying properly.

## 1. Fix the board's address on your own network

Your router hands out addresses by DHCP and will eventually give the board a different one, at which point your port forward points at your television. Fix it in one of two ways:

- **A DHCP reservation** in the router, tied to the board's MAC address. This is the better way: the board still asks for an address, the router always gives it the same one, and nothing on the board changes.
- **A static address** outside the DHCP pool, if you prefer to set it on the device.

The board's current address is on the `SYS` screen, along with its MAC and the network it joined.

## 2. Forward one port

In your router, forward **one TCP port** to `<board address>:6400`. Nothing else. No port ranges, no UDP, no DMZ.

The external port does not have to be 6400. Picking something else thins out the dumbest of the background scanning and costs nothing, because the people you want on the board are being told the number anyway. It is not a security control, for the reason given further down, so do not let it talk you out of anything else on this page. If you do change it, remember that callers now dial `yourname.example.com 2323` or whatever you chose.

Three things not to do, in order of how badly they end:

- **Do not put the board in the DMZ.** The DMZ forwards every port to it, which throws away the entire benefit of exposing a single-purpose device.
- **Do not forward the backup port.** `backup_port` (8080 by default) serves your config, your accounts and your screens over plain HTTP, and it only opens when somebody presses the button on the board while the sysop is logged in. It is a LAN tool. It has no business on the internet.
- **Do not leave UPnP on** and hope. UPnP lets anything on your network open its own forwards, which is the opposite of knowing what you have exposed.

## 3. Check it from outside

Your phone on cellular data is the easiest outside machine you own. Turn Wi-Fi off, open a terminal app (see [CLIENTS.md](CLIENTS.md)), and dial your public address. If it answers, you are live.

If it does not:

- Check the carrier-grade NAT question above, if you have not already. It is the most common reason a forward that looks right does nothing.
- Some providers block inbound ports, or forbid running servers in their terms. Worth two minutes of reading before you blame the board.
- Test from a genuinely outside network. Many routers will not let you reach your own public address from inside your own network, so a failure from your own sofa proves nothing.

## 4. Tell people

The board can also announce itself. The `announce` plugin posts a small heartbeat to a directory server every few minutes, which lists it for callers to find, and tells the board what its public address currently is. That last part makes it a rough dynamic DNS in its own right, because the directory always knows where the board answered from. It is off until you switch it on and it sends nothing about callers: see [ANNOUNCE.md](ANNOUNCE.md).

Directories and word of mouth both work. Use both.

A board nobody knows about is a board nobody calls. The telnet BBS scene is small, friendly and still keeps directories. Get yourself listed, and put the address somewhere people who would enjoy it will see it.

## What you are actually exposing

This is the part worth being precise about, because the instinct that "opening a port is dangerous" comes from a world of general-purpose computers, and this is not one.

Behind that port there is:

- **No operating system, no shell, no interpreter, no package manager.** There is nothing to drop a payload into and nothing that would run it if you did. The firmware is one static binary that does one thing.
- **No filesystem a caller can write to.** Accounts and screens change through forms, under permissions. The only bulk write path is the backup window, which is a different port, only opens when somebody physically presses a button on the board while the sysop is logged in, and stages and validates everything before the sysop approves it.
- **No dynamic memory in the caller path.** Static allocation, a fixed number of sessions, a fixed buffer per session, no heap in the main loop. A caller cannot make the board allocate, and cannot exhaust anything that was not already sized for them.
- **Nothing of value stored.** A handle, a salted hash, a profile line. No card numbers, no tokens, no keys to anything else.

Compare that with the things people routinely expose: a Linux box with SSH, a NAS with a web interface, a camera with a cloud account and a firmware update it fetches itself. Those are general-purpose computers with shells, credentials and package trees, and they are what the internet's automated scanning is built to find. A fixed-function microcontroller answering a text protocol on one port is a genuinely small target, and the worst realistic outcome of a bug in it is that it resets and comes back in a few seconds.

Small, though, is not zero. Here is what is actually left.

## The risks that are real

**1. Passwords cross the wire in clear.** Telnet has no encryption, by design and by necessity: a C64 cannot do TLS. Anybody on the path between a caller and your board can read what they type, including their password. That means:

- Tell callers plainly that this board's password must not be one they use anywhere else.
- Do not administer over the public port. Do sysop work from your own network or over a VPN. Staff passwords elevate access, so typing one across the internet is the one genuinely poor idea on this page.
- Treat every password on the board as though it will eventually be known, and design what you keep there accordingly.

This is not an oversight to apologise for, it is the cost of letting a 1982 machine call in, and it should be stated rather than glossed. For the formal version: [NIST SP 800-63B](https://pages.nist.gov/800-63-4/sp800-63b.html) requires that passwords only ever be requested over an authenticated protected channel. Telnet is not one. The salted SHA-256 store on the board protects the file if the file is stolen; it does nothing about the wire. The real risk is not your board, it is [credential stuffing](https://community.owasp.org/attacks/Credential_stuffing) somewhere else with a password a caller reused here.

**2. The board is a device on your network.** The interesting question is never "can somebody break the BBS", it is "what can they reach if they do". Put the board on your guest network or an IoT VLAN, so that what it can reach is the internet and nothing else. This is two minutes of work in most routers and it is the single highest-value thing on this page. If your router cannot do it, the next best thing is to know exactly what else is on that network and be comfortable with it.

**3. Somebody can occupy your lines.** There are six nodes. Idle timeouts, per-call and per-day limits, per-handle lockouts, IP bans and chat rate limits all exist and all help, but a determined nuisance with a script can still take up nodes. That is an annoyance, not a breach: `KICK`, `BANS` and `UNBAN` are there, and worst case you pull the port forward and they are talking to nothing.

**4. You will be found, and quickly.** Every address on the internet is swept constantly. Sophos put ten cloud honeypots online in 2019 and the first one was hit [52 seconds after going live](https://www.sophos.com/en-us/press/press-releases/2019/04/cybercriminals-attack-cloud-server-honeypot-within-52-seconds), averaging 13 attempts a minute afterwards. Expect first contact from background noise in minutes, not weeks.

Do not assume an unusual port number hides you either. [Shodan](https://www.shodan.io/) picks a random address and a random port from a curated list of around two thousand, and [6400 is on that list](https://book.shodan.io/behind-the-scenes/ports/). It also [fingerprints the protocol rather than trusting the port](https://book.shodan.io/behind-the-scenes/crawler-algorithm/), so a telnet service on any number gets recognised and banner-grabbed. A non-standard port thins out the dumbest bots and nothing more; treat it as noise reduction, never as a control.

Being found is not the same as being read, though, and the difference is the whole point. Somebody indexing a banner learns that a BBS answers there. Nothing crawls a board, logs in, sits in the room and scrapes the conversation, because there is no API to hand it over and no business model that wants it. Your board being listed and your board being harvested are different things.

**5. It is C++, and nobody has pen-tested it.** Say it plainly rather than pretend. The parsing paths use fixed buffers and bounds checks, the whole thing runs against a scripted test suite under AddressSanitizer and UndefinedBehaviorSanitizer on every build, and the design deliberately avoids the categories of bug that turn into remote code execution. But it has not been through an adversarial review, and anybody who tells you their C++ network code definitely has no memory bugs is guessing. The honest position: this is a hobby board on a five dollar chip. Put it on an isolated network, do not put anything behind it that matters, and enjoy it.

## The two settings that matter

If you read nothing else here:

- **Isolate the board.** Guest network or IoT VLAN. It removes the only failure mode with real consequences.
- **Never forward the backup port.** One port, 6400, and nothing else.

Everything after that is housekeeping.

## If you change your mind

Delete the port forward. The board is private again, immediately, and nothing about it needs reconfiguring. That is worth remembering: unlike a service you signed up for, there is no account to close, no data to request back and nobody to ask. You turn the one line off.

## Where to get yourself listed

| Directory | Notes |
|---|---|
| [Telnet BBS Guide](https://www.telnetbbsguide.com/) | the big one, around a thousand systems. [How to add yours](https://www.telnetbbsguide.com/faqs/how-to-add-your-bbs-listing/): register, check you are not already there, submit, wait for approval. Entries that stay dead for 30 days get removed |
| [The BBS Corner](https://bbscorner.com/) | its sister site, and the [telnet sysop pages](https://bbscorner.com/bbs-sysops-zone/telnet-bbs-info/) cover dynamic DNS and port forwarding from the BBS angle |
| [CBBS Outpost](https://www.commodorebbs.com/) | Commodore boards specifically. Free account, submit your own |
| [8-Bit Boyz](https://8bitboyz.com/bbs-directory/) | a clean, active 8-bit directory |
| [fsxNet](https://fsxnet.nz/fsxnet/join) | the modern hobby network, if you want message areas shared with other boards later |

## Reference

- Port forwarding, and the NAT it undoes: [port forwarding](https://en.wikipedia.org/wiki/Port_forwarding), [NAT](https://en.wikipedia.org/wiki/Network_address_translation).
- Why the DMZ is the wrong tool: [DMZ (computing)](https://en.wikipedia.org/wiki/DMZ_%28computing%29).
- Isolating the board: [VLAN](https://en.wikipedia.org/wiki/VLAN).
- Worked examples, vendor docs rather than ad farms: [pfSense port forwards](https://docs.netgate.com/pfsense/en/latest/nat/port-forwards.html) (it separates the NAT rule from the firewall rule, which is the part people get wrong), [OpenWrt firewall configuration](https://openwrt.org/docs/guide-user/firewall/firewall_configuration).
- Checking the port from outside: [CanYouSeeMe](https://www.canyouseeme.org/).
- What the internet already knows about your address: [Shodan](https://www.shodan.io/).

## See also

- [README.md](README.md): what the board is and why.
- [CLIENTS.md](CLIENTS.md): what your callers can dial in from.
- [COMMANDS.md](COMMANDS.md): every command, including `KICK`, `BANS` and `UNBAN`.
- [BACKUP.md](BACKUP.md): the backup window, and why it stays on your LAN.
