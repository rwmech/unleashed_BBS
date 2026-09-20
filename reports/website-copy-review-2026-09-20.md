# Website copy and render review, 2026-09-20

Scope: `unleashed_directory` at `C:\Users\rwmec\Documents\Development\BBS\unleashed_directory`.
Every page in `pages/`, every block of copy embedded in `server.py`, and the
rendered HTML and CSS for all thirteen routes.

Read only. Nothing in either repository was edited. The server was run on
127.0.0.1 ports 8791, 8792 and 8793 with `DIRECTORY_PAGE_CACHE=0` and a
scratch database. Nothing was sent to unleashedbbs.com or to any live host.

Where a fix needed proving rather than asserting, it was prototyped on a copy
of `server.py` in the scratchpad, rendered, and run against `selftest.py`:
**81 passed, 0 failed** with the P1.1 and P1.3 changes applied. The repository
copy is untouched.

Counts: **9 P1, 7 P2, 5 P3.**

---

## P1: broken or factually wrong

### P1.1 Every numbered step on the router pages renders as one run-on paragraph

**File:** `server.py`, `md_render()` around line 465. Affects
`pages/forward-netgear.md`, `pages/forward-asus.md`,
`pages/forward-xfinity.md`, `pages/forward-mesh.md`.

`md_render` supports `- ` bullets and nothing else. A line starting `1. ` falls
through to `para.append(...)`, and consecutive paragraph lines are joined with
`" ".join(para)`. There are **53 numbered steps across four pages** and every
one of them is currently a wall of text.

This is what `/forward-mesh` actually serves today:

```html
<p>1. Open the eero app and tap <b>Settings</b>, bottom right. 2. Tap
<b>Advanced networking</b>. 3. Tap <b>Reservations &amp; port forwarding</b>.
4. Add a reservation under <b>IPv4 Reservations &amp; Port Forwards</b>. 5.
Pick the board from the connected devices, or add it, and set its nickname,
MAC address and address. 6. Tap <b>Open a port</b>, then <b>Save</b>. 7. Enter
<code>6400</code>, give it a nickname, choose <b>TCP</b>. 8. Tap <b>Save</b>,
top right.</p>
```

Why it matters: these are the pages somebody reads with a router admin page
open in the other window, one step at a time. Eight steps mashed into a
paragraph is the difference between following instructions and losing your
place. Nothing caught it because the text is all present and in the right
order, which is exactly what grep checks. This is the same failure mode as the
three-stacked-warning-boxes bug in commit 6b3fd09.

**Fix.** Add ordered lists to the dialect. Four edits in `md_render`, plus one
regex. Prototyped and verified: `<ol>` with 33 `<li>` elements across the two
lists on `/forward-mesh`.

Next to `_MD_BOLD` (line 434):

```python
_MD_STEP   = re.compile(r"^\d{1,2}\. ")
```

In `md_render`, after the `out, para, bullets, code = ...` line:

```python
    steps = []            # "1. " lines: a numbered list, not a paragraph
```

Inside `flush()`, after the `bullets` block:

```python
            if steps:
                out.append("<ol>" + "".join(f"<li>{md_inline(s)}</li>"
                                            for s in steps) + "</ol>")
                steps.clear()
```

Replace the `- ` branch, and add the step branch after it:

```python
        elif line.startswith("- "):
            if para or steps:
                flush()
            bullets.append(line[2:])
        elif _MD_STEP.match(line):
            if para or bullets:
                flush()
            steps.append(_MD_STEP.sub("", line, count=1))
```

After the wrapped-bullet branch:

```python
        elif steps and raw.startswith("  "):
            steps[-1] += " " + line.strip()        # a wrapped step
```

And at the end of the function, after the trailing `bullets` block:

```python
    if steps:
        out.append("<ol>" + "".join(f"<li>{md_inline(s)}</li>" for s in steps) + "</ol>")
```

Then update the dialect list in `.claude/agents/docs.md` (BBS repo), which
currently says "No numbered lists".

### P1.2 NETGEAR step 4 collapses even after P1.1

**File:** `pages/forward-netgear.md` lines 43 to 48.

The five field labels are indented sub-bullets (`   - `). `md_render` checks
`line.startswith("- ")` against the un-lstripped line, so they never match, and
the `elif bullets and raw.startswith("  ")` continuation only fires when a
bullet list is already open. With P1.1 applied they end up inside step 4's
`<li>` as `Fill in: - Service Name: BBS - Service Type: TCP - ...`. Verified on
the patched render.

Nested bullets are deliberately not in the dialect and should stay out. Use a
table, which the dialect already supports. Replace lines 43 to 48 with:

```markdown
4. Click **Add Custom Service** and fill in the form below.
5. Click **Apply**.

| Field | Value |
|---|---|
| Service Name | `BBS` |
| Service Type | `TCP` |
| External Starting Port | `6400` |
| the ending port field | `6400` |
| Use the same port range for Internal port | leave ticked |
| Internal IP address | the address you reserved |
```

Keep the existing sentence underneath it, since it explains why one row is
described rather than quoted.

### P1.3 The callout boxes are not indented from the body text

**File:** `server.py` line 951.

The rule reaches the page and wins the cascade. It simply never sets a left
margin:

```css
article .warn { color:#f0c674; background:#241d10; border-left:3px solid #8a6d39;
        padding:10px 14px; margin:14px 0; max-width:78ch; }
```

`margin:14px 0` is a shorthand, so it sets `margin-left: 0` explicitly. The
box's left border lands flush with the left edge of every paragraph on the
page. The only offset a reader sees is the 3px border plus 14px of padding
pushing the text inside the box, which reads as a paragraph that has shifted
slightly rather than as a callout.

Specificity is not the problem and neither is delivery. `article .warn` is
(0,1,1); the later `article p { margin:0 0 14px; }` is (0,0,2) and loses.
There is exactly one `.warn` rule in the whole file, the `<p class="warn">` is
inside `<article>` on every page that has one, and the stylesheet is inlined in
a single `<style>` block. All four confirmed against the locally rendered
`/terminals`.

Why the earlier fix did not take: it was aimed at a different element. The
0.8.0 changelog entry reads "Pull quotes are indented and no longer align flush
with the body text", and `git log -S` confirms that commit moved `article
.pull` from `padding-left:12px; margin:18px 0` to `margin:22px 0 22px 36px`.
`article .warn` has never been touched since commit c2777fc introduced it. The
0.10.0 work on warnings (commit 6b3fd09) fixed one box per quote line, which is
a different symptom of the same element.

**Fix.** Match the indent step the page already uses. `.freedom` is at 30px and
`.pull` at 36px, so 30px is the house value for a box. Replace line 951:

```css
article .warn { color:#f0c674; background:#241d10; border-left:3px solid #8a6d39;
        padding:10px 16px; margin:18px 0 18px 30px; max-width:70ch; }
article ol, article ul { margin:0 0 14px; padding-left:28px; }
article ol li, article ul li { margin:0 0 6px; }
@media (max-width: 620px) {
  article .warn { margin-left:0; }
}
```

Three things in there beyond the indent. `max-width` drops from 78ch to 70ch so
the box still ends before the body text does once it starts 30px later, which
is what makes it read as a box rather than as a block. The list rule gives the
new `<ol>` from P1.1 the same spacing as the existing `<ul>`. The media query
matches the existing `.gallery.one` breakpoint: 30px out of a 343px phone
column is a real bite, and the border and background carry the meaning on their
own at that width.

Verified on the rendered page from the patched copy: the delivered CSS carries
`margin:18px 0 18px 30px` and the media query.

### P1.4 A one-domain deployment loses the manifesto and the data page, and three places say it does not

**Files:** `server.py` lines 76 to 78 and `do_GET`, `README.md` line 36,
`INSTALL.md` line 84.

`server.py` says:

```python
# One server, three faces, chosen by the Host header. A deployment with a
# single domain gets all three under paths instead, so none of this is
# required to run your own.
```

`README.md` says "Give the installer one domain and it serves everything".
`INSTALL.md` says "Give it one domain and that domain serves everything".

None of it is true. There are no path routes. `do_GET` handles `/`, `/static/`,
`/build`, `/rules`, `/how`, `/api/boards.json`, `/feed.xml`, `/health` and then
any file in `pages/`. The about and data faces are reachable only when
`role_for()` returns `about` or `data`, which needs `DIRECTORY_ABOUT_DOMAIN`
and `DIRECTORY_DATA_DOMAIN` set.

Verified locally with only `DIRECTORY_LIST_DOMAIN=one.example` set:

- `/about` returns 404
- `/data` returns 404
- the menu renders `What this is` and `Data` both pointing at `/`, the board list
- the manifesto text is nowhere in the response

Why it matters: the manifesto is the single best piece of writing on the site
and the whole argument for the project. On a one-domain install it cannot be
read, and two of seven menu items are loops back to the page you are already
on. Anybody following `INSTALL.md` to run their own directory gets that,
believing the docs.

**Fix, in `do_GET`, before the generic page branch.** `/about` and `/data` as
real routes, so the faces exist on any deployment and the Host header only
chooses the default:

```python
        elif path == "/about":
            self.reply(200, simple_page(
                "unleashed", ABOUT.replace("@GALLERY@", gallery_html()),
                role, "/about"))
        elif path == "/data":
            self.reply(200, cached("datapage", PAGE_CACHE,
                                   lambda: simple_page("Data", data_page(),
                                                       role, "/data")))
```

Then make `site_url` fall back to the path rather than to `/`:

```python
def site_url(target, role, path="/"):
    """A link to one of the three faces: absolute only when it crosses a
    domain, so a single-host deployment is never sent to a name that is not
    configured. With no domain for that face, it is served under its own
    path on this one."""
    host = {"list": LIST_DOMAIN, "about": ABOUT_DOMAIN, "data": DATA_DOMAIN}.get(target, "")
    if target == role:
        return path
    if not host:
        return {"list": "/", "about": "/about", "data": "/data"}.get(target, path)
    return f"https://{host}{path}"
```

`PAGE_NAME` is `^[a-z0-9][a-z0-9-]{0,39}$` and the page branch runs last, so
neither name can be shadowed by a file in `pages/`, and neither can shadow
`/health`. Add a `pages/about.md` or `pages/data.md` and the explicit branch
wins, which is the right way round.

Then correct the prose. `server.py` lines 76 to 78:

```python
# One server, three faces, chosen by the Host header. A deployment with a
# single domain serves the other two under /about and /data, so none of this
# is required to run your own.
```

`README.md` line 36, replacing "Give the installer one domain and it serves
everything; give it three and each gets its own face.":

```markdown
Give the installer three domains and each gets its own face. Give it one and
that domain serves the board list, with the other two faces under `/about` and
`/data`.
```

`INSTALL.md` line 84, replacing the first sentence:

```markdown
Give it one domain and that domain serves the board list, with the other two faces at `/about` and `/data`.
```

### P1.5 `deploy/Caddyfile` would take two of the three faces off the air

**File:** `deploy/Caddyfile`.

The checked-in file gives `unleashedbbs.com` a `reverse_proxy` block and then
sends `.net` and `.org` to it with `redir ... permanent`. The app never sees a
`.org` or `.net` Host header, so `role_for()` returns `list` for everything and
the manifesto and the data page become unreachable.

`deploy/setup.sh` does not use this file. It writes `/etc/caddy/Caddyfile` from
a heredoc that proxies every domain, with the comment "Every domain is served
rather than redirected: the server works out which face to show from the Host
header it is handed." So the two files disagree, and the one in the repository
is the wrong one.

Why it matters: it is dead config that reads as authoritative. Anybody running
their own directory by hand, rather than through `setup.sh`, copies it and gets
a site with two thirds of its pages unreachable and two dead menu items, with
no error anywhere to explain it.

**Fix, one of two.** Preferred: delete `deploy/Caddyfile` and have `INSTALL.md`
point at `setup.sh` as the only source of the web configuration. Otherwise,
replace the final block so it matches what `setup.sh` generates:

```
# --- the site itself: every domain is served, not redirected -----------------
# The server picks which of the three faces to show from the Host header, so
# a redirect here would collapse all three into the board list.
unleashedbbs.com, www.unleashedbbs.com,
unleashedbbs.net, www.unleashedbbs.net,
unleashedbbs.org, www.unleashedbbs.org {
	encode gzip
	reverse_proxy 127.0.0.1:8080
	log {
		output file /var/log/caddy/directory.log
		format console
	}
}
```

and add a line at the top of the file saying that `deploy/setup.sh` writes the
live one and this is a reference copy.

### P1.6 The manifesto says the board answers six callers

**File:** `server.py` line 1512.

Ground truth: `src/config.h:73` is `#define BBS_MAX_NODES 10`, plus a hidden
sysop node and a busy line. `CLAUDE.md` confirms ten since 0.17.0.

Replace:

```html
<p>CBBS answered one caller at a time on 64 kilobytes. This has eight times that
memory and answers ten at once, with a hidden eleventh line the sysop comes in
on. There is no operating system underneath it worth the name, no web stack, no
database, no container: the whole board is one program that fits in about a
megabyte and never allocates memory while a caller is typing.</p>
```

That also fixes P2.7 in the same sentence.

### P1.7 The build page says six callers

**File:** `pages/build.md` line 20.

Replace the `**Power.**` bullet:

```markdown
- **Power.** It runs from the USB port you flashed it with, a phone charger, or
  3V3 on a bench supply. A few tens of milliamps idling with ten callers on,
  with peaks when the radio transmits, so anything that can deliver 500 mA is
  comfortable.
```

### P1.8 "A guest does not even need that" is wrong

**File:** `server.py` line 1664, in the "No account, no email address, no phone
number" freedom box.

`USERS.md` line 158: "The guest keeps the handle they typed." A guest types a
handle like everybody else. What a guest skips is the password and the account.

Replace the box's paragraph:

```html
<p>A caller types a handle and picks a password, and that is the whole of
signing up. A guest types a handle and nothing else, gets fifteen minutes, and
leaves nothing behind. Nothing is verified because there is nothing to verify
against, and no identity is being assembled anywhere. Being unknown to a system
is the normal condition of being a person, and it should not require effort.</p>
```

Fifteen minutes is `guest_minutes`, default 15, per `USERS.md` line 172.

### P1.9 The curl example on `/how` loses its backslashes

**File:** `server.py`, the `HOW` string, line 1342.

`HOW` is a plain `"""` string, so a backslash at end of line is a Python line
continuation and is eaten along with the newline. The page serves:

```
curl -X POST http://unleashedbbs.net/announce   -H 'Content-Type: application/json'   -d '{"software":"synchronet","version":"3.20",
```

It still works if pasted, but it is not what was written, it scrolls sideways
on a phone, and the remaining lines are indented as if the continuations were
still there.

**Fix.** Make the string raw. There are no other escapes in `HOW` and it does
not end in a backslash:

```python
HOW = r"""<h1>How to get listed</h1>
```

---

## P2: confusing

### P2.1 `/how` and `/rules` are styled differently from every other prose page

**File:** `server.py`, `do_GET`, lines 1838 to 1841.

`md_page()` wraps its output in `<article>`. `RULES` and `HOW` are HTML
constants passed straight to `simple_page()` with no wrapper. Verified on the
rendered pages: `/build`, `/terminals`, `/forward`, `/dialing` and `/sdcard`
all contain `<article>`; `/how` and `/rules` do not.

So `/how`'s one `<h2>` gets the browser default, large and bold, instead of the
site's small cyan uppercase heading, and neither page picks up `article p`'s
margins or the 1.62 line height. Two pages in the menu and footer look like
they came from a different site.

**Fix.** Wrap both constants. In `RULES`, after the `<p class="lead">` line, add
`<article>` and close it with `</article>` before the closing `"""`. Same in
`HOW`. Keep `<h1>` and `<p class="lead">` outside the article, matching
`data_page()`, which already does exactly this.

### P2.2 `/how` and `/rules` highlight the wrong menu item

**File:** `server.py` lines 1838 to 1841.

Both call `simple_page(title, BODY)` with no `here`, so `nav_html` falls into
the `here in ("", "/")` branch and fills **BOARDS** as the current page.
Verified on the rendered `/rules` and `/how`: `<a class="here" href="/">Boards</a>`.

A reader on "Get listed" is told they are on "Boards". The menu blinks three
times on load to draw the eye to it, so it is not a subtle wrongness.

**Fix:**

```python
        elif path == "/rules":
            self.reply(200, simple_page("House rules", RULES, role, "/rules"))
        elif path == "/how":
            self.reply(200, simple_page("How to get listed", HOW, role, "/how"))
```

`/rules` is not in `NAV`, so it will simply highlight nothing, which is correct.
Passing `role` also stops these two pages serving the list-face footer to a
reader who arrived on the about domain.

### P2.3 The dialing page describes a layout the board list does not have

**File:** `pages/dialing.md` lines 19 to 22.

The warning says "Every address on this site is plain selectable text next to
the link, on purpose." In `board_rows` the address **is** the link text:
`<a href='telnet://...'>host port</a>`. There is no separate plain copy beside
it, and drag-selecting over an anchor starts a drag in most browsers.

`pages/terminals.md` line 47 makes a weaker version of the same claim and is
defensible; this one is not.

**Fix,** replacing lines 19 to 22:

```markdown
> You never have to touch any of this. The address in the Dial column is
> ordinary text inside the link: select it, copy it, and paste it into your
> terminal. Nothing below is required to call a board.
```

### P2.4 `/dialing` is nearly unreachable

**File:** `server.py` line 1167, `pages/terminals.md` line 44.

`/dialing` exists to fix the exact problem a first-time visitor hits: they
click an address on the front page and nothing happens. It is linked from one
sentence at the bottom of `/terminals`, and from a `title=` attribute on every
dial link that reads "See /dialing if nothing happens" as bare text. A `title`
is invisible on every touch device and is not clickable anywhere.

**Fix, two parts.** Put it in the footer, where "House rules" already lives.
In `foot_html`:

```python
    links = (f'<a href="{site_url("list", role, "/build")}">Build one</a> &middot; '
             f'<a href="{site_url("list", role, "/terminals")}">Terminals</a> &middot; '
             f'<a href="{site_url("list", role, "/dialing")}">Dial links</a> &middot; '
             f'<a href="{site_url("list", role, "/forward")}">Go public</a> &middot; '
             f'<a href="{site_url("list", role, "/how")}">Get listed</a> &middot; '
             f'<a href="{site_url("list", role, "/rules")}">House rules</a> &middot; '
             f'<a href="{site_url("list", role, "/feed.xml")}">RSS</a> &middot; '
             f'<a href="{site_url("data", role, "/api/boards.json")}">JSON</a>')
```

And name it in the index lead, `index_page()`, replacing the `<p class="lead">`:

```python
            + '<p class="lead">Boards that are up right now. '
            'Dial one with <a href="/terminals">any telnet client</a>, or click '
            'an address if you have one installed. '
            '<a href="/dialing">Nothing happened?</a></p>'
```

Then change the `title` on the dial link so it stops naming a URL as text:

```python
            f"<td class='addr'><a href='{dial}' title='Opens your terminal "
            f"program, if one is registered for telnet:// links.'>"
```

### P2.5 Nothing on the site says what happens after you connect

**Files:** new `pages/firstcall.md`, linked from `index_page()` and `NAV`.

The site covers finding a board, installing a terminal, building a board and
listing a board. It says nothing about the thirty seconds after a stranger
connects to one. They get a handle prompt with no idea whether to register,
whether it costs anything, or what a guest is.

Grounded in `USERS.md` lines 58, 115, 154 to 174 and `CLAUDE.md` 0.7.0.

```markdown
# Your first call

You have a terminal, you have picked a board off the list, and it answered.
Here is what happens next.

## It works out what you are

The board sends a short probe the moment you connect and reads what comes
back. ANSI with CP437 or UTF-8, PETSCII at 40 or 80 columns, or plain ASCII.
You configure nothing. A Commodore 64 gets a C64 screen and a laptop gets a
laptop one, and the two can sit in the same chat room.

If a board looks like line noise, the probe guessed wrong. Hang up, set your
terminal's character set to CP437, and call again.

## It asks for a handle

A handle is the name other callers see. It is not an email address and it is
not checked against anything.

Type a handle nobody on that board has taken and it offers you three things:

- **Register.** Pick a password, typed twice, and the board keeps an account
  for you: your profile, your messages, and however long the sysop allows you
  per day.
- **Guest.** No account and no password. You keep the handle you typed for
  that call, you get fifteen minutes, and nothing is saved. Lists mark you
  with a `*`.
- **A different handle.** If the one you wanted is taken.

Type a handle that already has an account and it asks for the password
instead. Three wrong tries and the board hangs up.

> Telnet has no encryption. Your password crosses the internet in the clear
> and so does everything you type. Use a password you use nowhere else, and
> say what you would say in public. [The longer version is
> here](/privacy).

## Then you are in

Type `?` for the menu. Every board is somebody's own arrangement, so the
commands differ, but a few are near universal: `WHO` for who else is on,
`CHAT` for the room, `PAGE` to get another caller's attention, `BYE` to hang
up. A board running this software also has `HELP` sections, so `? chat` shows
only the room commands.

There is a person behind it. If something is broken, or you want a feature,
the sysop's handle is on the listing and they will almost certainly answer.

## What a board knows about you

Your handle, your address, and when you called, in a log the sysop keeps so
they can see who has been on their own machine. If you registered, whatever
you typed into your profile. That is the whole list, and the sysop is the
only person who sees any of it.
```

Add to `NAV` after Terminals:

```python
       ("list",  "/firstcall", "First call"),
```

and a case in `selftest.py` alongside the other page names.

### P2.6 "Honest about the limits" is too thin, and there is no page behind it

**File:** `server.py` line 1701, plus a new `pages/privacy.md`.

This is the queued item in `CLAUDE.md`. The current paragraph names the
limitation and leaves the reader to imagine the risk, which they will do badly
in both directions. Replace the section:

```html
<h2>Honest about the limits</h2>

<p><b>Open communication over the internet is radio.</b> You transmit, whoever
is on the channel hears you, and that is the whole of it. A walkie-talkie, not
a sealed envelope. Telnet has no encryption, because a Commodore 64 cannot do
TLS and pretending otherwise would be worse than saying so.</p>

<p><b>Somebody has to be trying.</b> Being able to listen is not the same as
listening. It takes a packet sniffer or the equivalent, placed somewhere on the
path between a caller and the board. The board decides who hears what; the wire
carries it in the clear. If the radio is not switched on and tuned in, nobody
heard you.</p>

<p><b>The real risk is low and it is not zero, and the comparison is the
point.</b> These are public conversations. What would you say in a bar, or in a
coffee house, knowing the next table can hear? Now weigh that against a website
that records and ranks everything you do by design. A board is the bar. Yes,
somebody could be parked outside with equipment, and for almost everybody that
is an edge case. Saying so is more honest than implying it is either safe or
dangerous.</p>

<p><b>So: say what you would say in public, and use a password you use nowhere
else.</b> If a conversation has to survive somebody watching the link, put the
board behind a VPN or leave it on the local network. Privacy you can explain in
one sentence beats privacy you have to take on faith.</p>

<p><a href="/privacy">Read about the real risks of open communications</a></p>
```

Then the long version as `pages/privacy.md`. This uses no numbered lists, so it
renders correctly whether or not P1.1 has landed:

```markdown
# The real risks of open communications

A BBS carries everything in the clear. This page says what that actually means,
what it does not mean, and what to do about it. It is longer than the one
paragraph on the front page because the short version leaves people to guess,
and people guess badly in both directions.

## What "in the clear" means

When you call a board, your keystrokes travel as plain bytes. Your handle, your
password, what you type in the chat room, what you read. Anybody who can see
the traffic on the path between you and the board can read all of it.

There is no encryption to turn on. The protocol is telnet, from 1969, and the
machines this is built for cannot do better: a Commodore 64 has no room for a
TLS stack and never will. The honest move is to say so rather than to add a
padlock that means nothing.

## Who can actually see it

Not "anybody on the internet". Somebody on the path, running a tool, on purpose.
In practice that is:

- Anybody on the same wifi as you, if it is open or if they have the key.
  Coffee shops, hotels, conferences, airports.
- Whoever runs the network you are on. An employer, a university, a landlord.
- Your internet provider, and the board's.
- Anybody who has got into a router between the two of you.

Being able to listen and listening are different things. Every one of those
requires a person choosing to point a tool at your traffic. None of it happens
by itself, none of it is automatic, and none of it is being collected and kept
by default, which is the part that makes this different from the web.

## What it is not

It is not a website. A website logs your address, sets an identifier in your
browser, records what you read and how long for, hands it to an analytics
company, and keeps it under a retention policy you never read. All of that is
by design and all of it happens whether or not anybody is interested in you.

A board does none of that. There is no third party in the middle because there
is nowhere for a copy to go. The trade is real, and it runs both ways: the
conversation is readable by somebody who is trying, and it is not being
harvested by anybody who is not.

## The comparison that helps

Think of a bar, or a coffee house. You talk, and the next table could hear you
if they cared to. Most of the time nobody does. You still would not read your
bank details out loud, and you would still say most of what you came to say.

That is the right model for a BBS. It is a public room. Somebody could be
parked outside with equipment, and for almost everybody that is an edge case
rather than a plan.

## What to actually do

> Use a password you use nowhere else. This is the one that matters. A password
> read off the wire is only worth what it unlocks elsewhere, so make that
> nothing.

- Say what you would say in public. Treat the chat room as a room, because it
  is one.
- Do not type anything into a board that would hurt you if it were read out.
  Card numbers, other passwords, an address you would not give a stranger.
- On a network you do not trust, assume somebody could be looking. Open wifi is
  the realistic case.
- If a conversation genuinely has to be private, this is the wrong tool. Put
  the board behind a VPN, or keep it on your own network, or use something
  built for secrecy.

## If you run a board

Tell your callers before they pick a password, not afterwards. A board running
this software does that by itself: it warns at sign-up, offers to explain, and
the `PRIVACY` command replays the explanation any time.

Keep in mind what your own caller log holds. Handles, addresses and call times
are a record of who has been on your machine, which is why it exists, and it is
yours to look after.
```

Link it from `pages/terminals.md`, where the telnet warning already sits, by
changing the end of that warning to:

```markdown
> telnet board. [What that actually risks](/privacy).
```

Add a case in `selftest.py` with the other page names.

### P2.7 "fits in a megabyte" is now marginally false

**File:** `server.py` line 1515.

`CLAUDE.md` 0.17.1 Block C records the image at 68.4% of the 1.5 MB slot, which
is about 1,050 KB. Covered by the P1.6 replacement text above, which says
"about a megabyte".

---

## P3: polish

### P3.1 Currency and idiom drift

**File:** `pages/terminals.md` line 108, `pages/build.md` line 3.

The site is written from Illinois and prices things in dollars: "a five dollar
chip", "about two dollars". `terminals.md` says a bridge costs "a few pounds",
which reads as sterling next to those. `build.md` says "less than a takeaway",
which is British for takeout.

Replace `terminals.md` lines 106 to 109:

```markdown
Hardware ones for Commodore and Atari are listed above. On anything with a
serial port, a Raspberry Pi running `tcpser`, or an ESP32 running
[Zimodem](https://github.com/bozimmerman/Zimodem), does the same job for a few
dollars. A real modem and a real phone line also still work, if you have both.
```

And `build.md` line 3:

```markdown
A board of your own, on hardware that costs less than lunch.
```

which also matches `README.md` line 65.

### P3.2 The index footer note is one unbroken block

**File:** `server.py`, `index_page()`, the `foot_html` extra.

Five clauses and 60 words with no break, under a table that has just used six
column headings. It is the only place that explains what "Activity" and
"Up for" mean, so it is worth reading and currently is not.

```python
    footer = foot_html("list",
        'Activity is the last 24 hours: how many calls, and how long callers '
        'were connected in total.<br>'
        'Caller counts and activity are reported by the boards themselves. The '
        'small figure next to the state is how old that reading is.<br>'
        '"Up for" is measured here and cannot be fudged.')
```

### P3.3 The manifesto lists doors as something the board has

**File:** `server.py` line 1541.

"Nodes, handles, a user list, a chat room in the style of DDial and Gtalk,
messages, doors, a caller log, a sysop who can page you."

Doors are not built. `CLAUDE.md` has Lua doors in the queue and is explicit
that a Lua core is its own decision. File areas landed in 0.17.1 and are not in
the list; message bases are not built either.

The BBS `README.md` line 65 carries the same claim, so the two are consistent
and this is a judgement call for Rob rather than a straightforward correction.
If he wants it made true today:

```html
<p>A telnet BBS that runs on a bare ESP32 and grows through plugins. Nodes,
handles, a user list, a chat room in the style of DDial and Gtalk, messages,
file areas on an SD card, a caller log, a sysop who can page you. Message bases
and doors are next.</p>
```

If it stays as it is, `README.md` line 65 should stay as it is too. Change one,
change the other.

### P3.4 A `telnet://` link to an IPv6 literal is malformed

**File:** `server.py` line 1157.

```python
dial = html.escape(f"telnet://{where}:{r['port']}", quote=True)
```

`where` is the board's host or its address. An IPv6 literal needs square
brackets, or the colons in the address run into the port. No listed board hits
this today, and `pages/dialing.md` line 185 already brackets correctly in its
PowerShell handler, so the site explains a URL shape it does not emit.

```python
        where = r["host"] or r["address"]
        # An IPv6 literal needs brackets or its own colons run into the port.
        target = f"[{where}]" if ":" in where else where
```

and use `target` in the `dial` line. Leave the visible text as `where`.

### P3.5 Tests for everything above

**File:** `selftest.py`.

The existing suite counts warning boxes, which is how the three-box bug was
caught, so the pattern is already right. Add:

```python
        code, page = get("/forward-mesh")
        check("numbered steps are a list, not a run-on paragraph",
              "<ol>" in page and "<li>Tap <b>Advanced networking</b>.</li>" in page)
        check("and no step number survives as text", ">1. Open the eero" not in page)
        code, page = get("/how")
        check("get listed is styled like every other prose page",
              "<article>" in page)
        check("and the menu knows which page it is on",
              '<a class="here" href="/how">' in page)
        check("the curl example keeps its line continuations",
              "announce \\\n" in page or "announce \\" in page)
        code, page = get("/about", host="boards.example")
        check("the manifesto is reachable without its own domain",
              code == 200 and "A bulletin board is a machine" in page)
        code, page = get("/data", host="boards.example")
        check("and so is the data page", code == 200 and "Endpoints" in page)
        code, page = get("/privacy")
        check("the privacy page exists and leads with the radio framing",
              code == 200 and "in the clear" in page)
        code, page = get("/firstcall")
        check("and so does the first call page",
              code == 200 and "handle" in page)
```

There is no good automated check for the callout indent. A CSS assertion would
only prove the string is in the file, which is the failure mode this whole pass
exists to avoid. Look at the rendered page instead, at full width and at 400px.

---

## Checked and correct

Worth recording so the next pass does not redo it.

- Port 6400 everywhere on the site, matching the board.
- `servers = http://unleashedbbs.net/announce` on `/how` matches
  `src/plugins/announce.cpp:683`.
- "A listing becomes public after three hours" matches `PENDING_HOURS`, and
  "`ANNOUNCE` on your board shows how long is left" matches the `public in
  %uh%02um` line in the plugin.
- "The activity LED holds on for a second once the board is actually listening"
  matches `plat::ledSignal(plat::millis(), 1000)` in `src/main.cpp:206`.
- `unleashed.local`, the `hostname` setting doing double duty, and the
  `online <ip>  dial in: telnet <ip> 6400` console line all match `README.md`.
- The SD card page against 0.17.1 Block B: FAT32 only, 3V3 not VIN, GPIO5
  strapping, the three mount failure messages, the split between card and
  LittleFS, per-file screen override. All correct.
- The flashall and backup paragraphs on `/build` match the 0.14.0 partition
  split. No stale "flashing wipes your accounts" warning anywhere on the site.
- Blockquotes render as one box per quote, not one per line. That fix did land.
- All thirteen routes return 200 and `/nosuchpage` returns 404.
- `selftest.py` is 81 passed, 0 failed, both before and after the prototyped
  P1.1 and P1.3 changes.

## Suggested order

P1.1 and P1.2 together, since the second is only visible once the first lands.
Then P1.3, which is one rule and has been outstanding through two reports. Then
P1.4 and P1.5 together, since they are the same defect seen from the code side
and the deploy side. P1.6 through P1.9 are single-line corrections and can ride
with any of it. P2.1 and P2.2 are four lines in `do_GET` and belong in the same
commit. P2.5 and P2.6 are new writing and are their own pass.
