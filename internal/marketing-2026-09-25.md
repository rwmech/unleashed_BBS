# Front page positioning: three options (marketing, 2026-09-25)

Rob, on the live hero ("A whole BBS on a board the size of a stick of gum,
for about $5. It answers an 8-bit computer from the eighties and a laptop
from this year in the same chat room, on about half a watt."): "It sounds
like Yet another BBS when it does so much more and we didn't even mention
retrocomputing, modern communities, etc."

This is a proposal. Nothing on the site or in the firmware has changed. Rob
picks; the approved lines go to `explain` to place.

## The diagnosis in one paragraph

The current hero sells three specs (size, price, power) and one demo (old and
new in one room). All four are true and all four are answers to "how", so a
reader who does not already want a BBS has no reason to care about them. It
never says why you would want a board at all, and that "why" is exactly what
separates this from the other BBS packages, which all assume you arrived
wanting one. The fix is to lead with the reason (a place you own, somewhere
for your machine to call, a thing you can hold) and let the specs prove it.

## What the research says

### How comparable projects pitch themselves

| Project | Hero, as written | Shape |
|---|---|---|
| Meshtastic | "Off-Grid Communication For Everyone" / "No cell towers. No internet. Just pure peer-to-peer connectivity." | the reason first, defined by what it frees you from |
| ESPHome | "Custom smart home devices, built by you" / "When you can't find the smart home device you need in a store, build it" | ownership and making |
| Tasmota | "Total local control with quick setup and updates" | control, then ease |
| FujiNet | "The Future of Retro Computing" / "a multi-peripheral emulator and WiFi network device for vintage computers" | old machine meets modern network |
| TeensyROM | "Connect your Commodore to the 21st century" | the same bridge, one machine |
| WLED | "A fast and feature-rich implementation of an ESP32 webserver to control NeoPixel ... LEDs" | spec-led; succeeds on its demos and installer, not its words |

Sources: [meshtastic.org](https://meshtastic.org/), [esphome.io](https://esphome.io/),
[Tasmota docs](https://tasmota.github.io/docs/), [fujinet.online](https://fujinet.online/),
[TeensyROM README](https://github.com/SensoriumEmbedded/TeensyROM),
[WLED docs](https://kno.wled.ge/), [WLED installer](https://install.wled.me/).

The pattern: the maker projects that travel lead with a benefit a person
feels (control, ownership, freedom from infrastructure), then prove it with
"easy". The retro devices lead with the bridge between an old machine and
the modern world. Meshtastic is the closest analogue in spirit: small cheap
radios, no provider, and a hero built from what you no longer need.

### What the other BBS packages lead with

- Synchronet: "a free, open-source package for running your own online BBS
  supporting multiple simultaneous users, hierarchical message and file
  areas, multi-user chat, and classic BBS door games"
  ([wiki](https://wiki.synchro.net/)). Its front page is a news feed
  ([synchro.net](https://www.synchro.net/)).
- Mystic: no self-description on its front page at all, just art and news
  ([mysticbbs.com](https://www.mysticbbs.com/)).
- ENiGMA½: "Modern bulletin board software with a nostalgic flair", then a
  wall of feature headings: "THREE WAYS IN", "FIDONET, NATIVELY", "DOS DOORS
  THAT RUN", "YOURS TO SHAPE" ([enigma-bbs.github.io](https://enigma-bbs.github.io/)).

All three talk to somebody who already runs a server and already wants a
BBS. None of them says why anybody should want one, and none of them has a
physical object to show. That is the open ground: **the other packages sell
software to sysops; µnleashed can sell the reason and the object to people
who have never been one.** The current hero reads like them because it is
written in their register (specs and capability) rather than in the maker
register (a benefit you feel, then how easy it is).

### The audience, and where it gathers

- **The BBS revival is real and steady, not huge.** The Telnet BBS Guide lists
  1,009 boards, with 18 added this month; Synchronet runs 298 of them, Mystic
  190, ENiGMA½ 41. Only 77 offer SSH, so plain telnet is the norm there and
  not an objection ([telnetbbsguide.com](https://www.telnetbbsguide.com/)).
  Worth listing µnleashed boards there as well as on our own directory: it is
  where people already go looking for something to call, and a new software
  name on it is visibility.
- **New vintage-style hardware is creating new callers.** The Commodore 64
  Ultimate shipped with Wi-Fi networking and BBS calling as a selling point;
  one review puts sales at 20,000 units
  ([Nerdly Pleasures](http://nerdlypleasures.blogspot.com/2026/03/the-ultimate-commodore-64-commodore-64.html)),
  and its owners ask each other which boards to join
  ([Lemon64 thread](https://www.lemon64.com/forum/viewtopic.php?t=88452)).
  FujiNet does the same for Atari, Apple and other families. These people
  have a machine that can call and are looking for somewhere to call. That
  is option B's whole argument. (This is outreach targeting, not a favourite
  machine on the site: the site names the class; the posts go to each
  family's own forum.)
- **Why the revival appeals, in the press's words.** Hackaday's January 2026
  piece calls the BBS era "a more wholesome and less angsty time than the
  modern algorithm-driven Internet" and the trend "rejecting more modern
  technology and systems in favor of older ones, where the users had more
  control" ([Hackaday](https://hackaday.com/2026/01/25/commodore-64-helps-revive-the-bbs-days/)).
  Control and the absence of an algorithm are the emotional core, and they
  are option A's argument.
- **The platforms are giving that argument away this week.** Discord began
  rolling out age verification to all users on 23 September 2026 after a
  delay forced by backlash and an earlier breach of ID photos
  ([TechCrunch](https://techcrunch.com/2026/09/22/discords-age-verification-era-is-upon-us-despite-community-backlash/)).
  The "cozy web" of small group spaces moved onto platforms like it
  ([Maggie Appleton](https://maggieappleton.com/cozy-web)); people who
  wanted a small room discovered they were renting it. Do not name any
  platform on the site (it dates the copy and picks a fight), but this is
  why "a place you own" lands now and may not have a year ago.
- **Where they gather.** Hackaday and its tips line (tag
  [bbs](https://hackaday.com/tag/bbs/): seven BBS stories since 2023, one
  of them a PETSCII BBS framework, so not a first); the family forums
  (Lemon64, AtariAge, the VCFed forum); r/retrobattlestations, r/bbs and
  r/vintagecomputing; the Vintage Computer Festivals, East, West, Midwest and
  PNW ([Wikipedia](https://en.wikipedia.org/wiki/Vintage_Computer_Festival));
  and the Telnet BBS Guide.
- **What makes them share.** Real machines doing a real thing, photographed.
  r/retrobattlestations has run group activities that are exactly "dial
  the BBS from whatever you have" and members wrote up their attempts
  ([Kevin Hooke](https://www.kevinhooke.com/2017/04/15/retro-battlestation-update-2-dialing-up-a-local-bbs-with-the-2002-power-mac-g4-quicksilver/)).
  Hackaday runs things with a build, a photo and a price. A board in a
  hand, a CRT showing the board's screen, a pixel strip lit by callers:
  those are the shareable objects, and the front page has none of them yet.

## The three options

Every claim below was checked against README.md, CLIENTS.md, CHANGELOG.md
1.1.0, and the site's /different, /hardware, /roadmap, /camera and /lights.
Items tagged **New in 1.1** need firmware 1.1.0 (lights on the dev board,
the camera on the Freenove board, the S3's screen as a full release rather
than a preview); until 1.1.0 is on disk the site's `::: until` gates should
show them as coming. Items tagged **Coming** are on /roadmap under Later and
are not built.

### Option A: a place you own (community first)

- **Positioning:** For clubs, friends and families tired of renting their
  community from a platform, µnleashed is a bulletin board you own outright:
  a $5 board on your shelf that old computers and new ones both call.
- **Kicker:** A real bulletin board system, on hardware you own
- **Hero:** Your own place online, on a $5 board.
- **Sub-head:** µnleashed turns a tiny ESP32 into a whole BBS on your shelf:
  a chat room and mail for ten callers at once, and forums and a file library
  once you add an SD card. No platform, no feed, no terms but yours. A 1980s
  8-bit and this year's laptop meet in the same room.
- **Features:**
  - **No platform in the middle.** The member list is a text file on a chip
    you own. Nothing is indexed, ranked, scraped or sold.
  - **Every machine is welcome.** Vintage 8-bits, glass terminals, PCs and
    phones. It works out each caller's terminal as they connect.
  - **Small by design.** Ten lines and a busy signal, one chat room, handles
    in the margin. A room, not a feed.
  - **Hardware you can watch.** (New in 1.1) Lights that wake as callers
    arrive, a status screen on the S3, a camera callers can use.
  - **Up in five minutes.** Install from Chrome or Edge and set Wi-Fi on the
    same page. About $5, about half a watt.
- **Call to action:** "Start your board" (to /build), with "Call a board now"
  (to the list below) beside it.
- **Other heads considered:** "Own the room." (punchier, but the idiom means
  "dominate", and it needs the sub to explain it); "Your people, your board,
  your shelf."
- **Why:** It answers Rob's complaint directly: a BBS is no longer the
  product, it is the proof. It is the one thing none of Synchronet, Mystic or
  ENiGMA½ says, it matches the emotional core the press names ("users had
  more control", no algorithm), and it is timely. It reaches past the
  existing BBS crowd to the clubs, classrooms and families /whofor already
  addresses. Risk: "own your community" is a message other projects use too
  (self-hosted forums, Mastodon), so the sub has to land the specific,
  physical, $5 version fast, which it does.

### Option B: somewhere for the old machines to call (retro first)

- **Positioning:** For anybody with a vintage computer that can get on the
  wire, µnleashed is the BBS built to answer it properly, on a $5 board you
  run yourself.
- **Kicker:** A BBS that speaks your computer's language
- **Hero:** Your old computer has somewhere to call again.
- **Sub-head:** An Amiga, an Atari 800, a VT220 and this year's laptop, in
  one chat room. µnleashed is a whole BBS on a $5 ESP32 that recognises each
  caller's terminal as they connect and draws the screen to fit: PETSCII at
  40 columns, ANSI art at 80, or plain ASCII.
- **Features:**
  - **Detected, not configured.** PETSCII at 40 or 80 columns, ANSI in CP437
    or UTF-8, or plain ASCII, worked out as you connect.
  - **Ten lines and a busy signal.** Ten callers at once. The eleventh gets a
    busy screen and a countdown, like the phone line did.
  - **Chat the way it was.** One room, one line at a time, handles and ranks
    in the margin, in the spirit of Diversi-DIAL.
  - **A software library on a card.** Disk images and utilities on a micro SD
    card, downloaded by XMODEM or YMODEM.
  - **One superchat.** (Coming) Chat rooms linked across boards: µnleashed,
    Diversi-DIAL and GTalk-style systems, together.
- **Call to action:** "Dial a board tonight" (to the list), with "Put your
  own on the air" (to /build).
- **Why:** The retro audience is the one most likely to share this, and it
  is growing: machines like the C64 Ultimate and FujiNet-equipped Ataris ship
  able to call and their owners are looking for boards. The CTA sends them to
  call first, which feeds the directory, and building follows. Risk: it is
  the narrowest of the three, and it pulls toward "nostalgia", which is the
  "yet another BBS" register Rob wants out of. It makes a better second
  page, or a post in each family's forum, than a front page.

### Option C: the whole BBS fits in your hand (maker first)

- **Positioning:** For makers who like hardware that does something visible,
  µnleashed is firmware that turns a $5 microcontroller into a complete BBS,
  with no PC or Pi under it, and lights, a screen and a camera the callers
  can drive.
- **Kicker:** Firmware that turns a microcontroller into a BBS
- **Hero:** The whole BBS fits in your hand.
- **Sub-head:** No PC, no Raspberry Pi, nothing to patch: a $5 ESP32 is the
  entire board. Flash it from your browser in five minutes, then watch it
  work. Pixels light as callers arrive, a screen shows who is on, and a
  caller on a forty-year-old terminal can take a photo with its camera.
- **Features:**
  - **Flash it from a browser.** Plug it in, press Install, set Wi-Fi on the
    same page. No toolchain, no command line.
  - **Lights that show who's on.** (New in 1.1) A drive light for the storage
    and a pixel strip that fills as callers dial in.
  - **A screen of its own.** (New in 1.1) The $20 Waveshare S3 shows the
    board's name, its address and who is on.
  - **A camera callers can use.** (New in 1.1) Type SNAPSHOT from any terminal
    and download the photo seconds later.
  - **Sensors on the pins.** (Coming) Temperature, motion and switches, named
    and shown on screen without writing code.
- **Call to action:** "Flash one now" (to /install), with "Pick a board" (to
  /hardware).
- **Why:** This is the Hackaday and WLED register: an object, a price, a
  demo, an installer. It is the most concrete ("snap the birdfeeder from a
  1982 computer" is in this option's DNA) and the one a photo sells best.
  Risk: three of five features need 1.1.0 and one is not built, so before
  1.1.0 ships this option is mostly promises; and it undersells the
  community half.
- **Wording checked:** "nothing to patch" rather than /different's "no
  operating system": the firmware does run FreeRTOS under ESP-IDF, so "no
  operating system" can be picked apart; "nothing to patch" is what the
  reader actually cares about and is true.

## Recommendation: A, with B and C carried by the strip

Lead with **Option A**. It is the one that stops the page reading as "yet
another BBS", because the BBS becomes the evidence rather than the pitch, and
it is the message nobody else in this space is making. A's feature strip
already carries B (every machine welcome) and C (hardware you can watch, five
minutes), so the retrocomputing, modern community, maker and easy points
from Rob's checklist are all above the fold.

Use B and C where they are stronger than a front page:

- **B** as the opening of /terminals or /firstcall, and as the post to each
  family's forum (Lemon64, AtariAge, VCFed, r/retrobattlestations).
- **C** as the opening of /hardware and as the Hackaday tip: one photo of the
  board in a hand with the pixel strip lit, one of a vintage screen showing
  a SNAPSHOT download.

Before 1.1.0 ships, A's fourth card should read "coming with 1.1" rather
than "New in 1.1"; the site's gates already do that for /different.

## Mockups

Built from the live front page's own HTML and CSS (served by server.py on
127.0.0.1 with an empty database, which is why the directory below says "no
boards yet"), with Oxanium self-hosted as on the site. Only the opening row
is replaced; the masthead, nav, ticker and "Run your own board" card are the
site's own. The tags use the site's warm and name colours; each card's top
edge takes one palette colour in turn.

In the session scratchpad, `marketing/`:
`option-a-1366.png`, `option-a-390.png`, `option-b-1366.png`,
`option-b-390.png`, `option-c-1366.png`, `option-c-390.png`, and the
HTML (`mock-a.html` etc., `build_mocks.py` builds them).

## Beyond the hero

- **Show the object and the screen.** The front page has no photograph of a
  board and no picture of what a caller sees. The audiences here share
  photographs of real machines doing real things. Add a strip under the
  hero: the board in a hand, a vintage screen showing the board's PETSCII
  login, an ANSI screen on a PC, the S3's panel. Real captures only (Rob's
  standing rule for the S3 panel applies to all of them).
- **Put a live figure next to the hero.** "N boards up, N callers on right
  now" currently sits below the rule. A live number is proof for the retro
  visitor who wants something to call tonight; move it up once there are
  enough boards that it helps.
- **Give people a thing to do together.** A "dial-in week" in the
  r/retrobattlestations style: call a listed board from the oldest machine
  you own and post the photo. It feeds the directory and produces exactly
  the pictures the page is missing.
- **List boards on the Telnet BBS Guide as well.** That is where existing BBS
  callers look, 1,009 boards and growing, and µnleashed as a software name
  there is free visibility.
- **Say "new in 1.1" loudly when it ships.** Lights, the camera and the S3
  screen are the most shareable things the project has; the release
  announcement should lead with a photo of each rather than the changelog.

---

# Round 2 (same day): building a community, machine-agnostic

Rob, after the three mockups: "I think the messaging is still lacking. We
need it more agnostic. Like build a community on a chip that fits in the
palm of your hand. Appealing to wanting to build a community. Alternatively
we could take 3 or 4 of the strongest messages and have those change up on
each page load or in FX fashion." He wants mass engagement and builds, real
photos (he will take them), a Telnet BBS Guide listing, and a vintage
community look and feel.

He is right, and it sharpens Option A. The round 1 options each chose an
audience (communities, retro owners, makers); Rob's seed chooses a verb
instead. "Build a community" is something every one of those audiences
wants, and it puts the reader in charge of the sentence. The machine, the
chip and the price become the proof underneath.

## 1. Hero lines, community first

The proof belongs in the sub-head and the cards, never in the headline: a
$5 board, anything with a terminal can call, minutes to build. Every line
below is checked against the round 1 sources. "Fits in your palm" is true
of every supported board: the dev board is about the size of a stick of
gum (the site's own figure on /different) and the Waveshare S3 is a USB
stick.

- **Build a community on a chip that fits in your palm.** (Rob's seed,
  tightened: "the palm of your hand" is two extra words carrying nothing)
  - Sub: µnleashed turns a $5 ESP32 into a whole bulletin board for your
    people, on hardware you own: a chat room and mail, and forums once you add
    an SD card. Anything with a terminal can call, from a forty-year-old 8-bit
    to the laptop on your desk, and it goes from the box to its first caller in
    about five minutes.
- **Your people. Your rules. Your board.**
  - Sub: A complete bulletin board on a $5 chip you own. No platform in the
    middle, no feed, no terms but the ones you write. Anyone with a terminal can
    call, old machine or new.
- **Start a place people want to call.**
  - Sub: A chat room and mail for ten callers at once (forums with an SD
    card), on a board that fits in
    your palm and costs about $5. Install it from your browser in minutes, then
    hand out the address.
- **No platform. No feed. Just your people.**
  - Sub: µnleashed is a bulletin board on a $5 chip on your shelf. The member
    list is a text file you can read, and the only algorithm is who shows up.
- **A whole community, in the palm of your hand.**
  - Sub: A chat room and mail on a $5 ESP32, and forums and a file library
    once you add an SD card. Vintage
    computers, glass terminals, laptops and phones all dial the same board.
- **Build the room. Anyone can dial in.**
  - Sub: A $5 board becomes a bulletin board in about five minutes. It works
    out each caller's terminal as they connect, so whatever they call from, it
    looks right.

Feature cards for the recommended hero (rendered in the mockups):

- **Everything a community needs.** A chat room, mail, forums and a file
  library, with handles, ranks and a sysop. Forums and files need an SD card.
- **Anyone can call.** Vintage 8-bits, glass terminals, PCs and phones. Each
  caller's terminal is worked out as they connect.
- **Yours, not a platform's.** No feed, no ads, no terms but yours. The member
  list is a text file you can read.
- **Built in minutes.** Install from Chrome or Edge, set Wi-Fi on the same
  page. About $5, about half a watt.
- **Found by the right people.** Switch on the listing and your board appears
  in this directory by itself.

CTA: **Build your board** (to /build), with **Or call one first** (to the
list). "Build" rather than "Install": it is the verb of the headline, and
/build is where somebody without a board starts.

**Recommended: the first line, fixed.** It is Rob's, it is the only one
that names both halves of the product (a community, and a chip) in one
breath, and "build" is the action we want. "Your people. Your rules. Your
board." is the strongest runner-up and makes a good social card or sticker.

## 2. Rotating heroes: my honest opinion

**Recommendation: one fixed headline. If there is to be variety, rotate the
small audience line above it on the server ("For a retro computing club",
"For a ham radio net", "For a classroom", "For friends who miss the old
internet"), with no animation.** If Rob wants motion as well, the fallback is
the CSS cycle in the mockups, with a pause control and the brand line first.

The reasoning, point by point:

- **Engagement.** Rotation in marketing earns its keep as a test: show
  several lines, measure which converts, and keep the winner. This site has
  no analytics by design ("No analytics, no telemetry, no engagement metric"
  is in server.py's own copy), so there is nothing to learn from. Without
  the measuring, rotation is only variety, and for a first-time visitor
  variety buys nothing: they see one line once. A repeat visitor sees a
  different claim each time, which reads as a brand that has not decided
  what it is.
- **The page already rotates.** The masthead's "Electronic freedom" ticker
  cycles eight lines on a 32 second CSS loop. A second rotating element a
  few centimetres below gives the page two things moving at different
  speeds, competing for the eye at exactly the point the headline should
  own it. If we want the other lines on screen, the ticker is the place:
  swap two of its eight items for "Build a community" and "Your people.
  Your rules." and nothing new has to move.
- **Search engines and link previews.** Server-side per load: whichever line
  was cached when the crawler came is what gets indexed, and the front page
  is cached whole for `PAGE_CACHE` (10 s), so "per page load" is really "per
  ten-second window". CSS cycle: every line is in the HTML, so the page has
  four headlines' worth of text as its top heading unless only the first is
  in the `<h1>`. Either way, **link previews do not read the hero at all**:
  they read `og:description`, which on the front page today is "N bulletin
  boards listed, N up right now, N callers on", and `og:image` is the logo
  avatar. Somebody sharing the front page on a forum shares a board count.
  Fixing that is worth more than any rotation: set the front page's
  `og:description` to the hero and its first sub-head sentence, and its
  `og:image` to photo 2 below once it exists.
- **Accessibility and reduced motion.** WCAG 2.2 SC 2.2.2, Level A: moving
  content that starts by itself, lasts more than five seconds and sits
  beside other content needs a way to pause, stop or hide it
  ([W3C](https://www.w3.org/WAI/WCAG22/Understanding/pause-stop-hide.html)).
  A cycle through four lines is longer than five seconds, so it needs the
  pause control in the mockup (a CSS-only checkbox), `prefers-reduced-motion`
  to show the first line still, and the other lines `aria-hidden` so a screen
  reader hears one headline rather than four. That is buildable without a
  script, but it is three things to get right for a benefit we cannot
  measure. (The masthead ticker is under the same rule; worth checking it
  has a stop.)
- **Brand.** The brand is what somebody repeats to a friend. One line
  repeated is a brand; four lines alternating are a mood. The audience line
  is different: rotating *who it is for* while holding *what it is* fixed
  makes each visitor more likely to see themselves in it, and it costs
  nothing in consistency.

Server-side kicker rotation needs no JavaScript and no motion, fits the
site's cache (one choice per ten seconds is plenty), and gives Rob the
"change up on each page load" he asked for in the one place it helps.

## 3. The vintage community look

Judged for cost to build and whether it gets shared. All of it stays in
the site's current palette and monospace setting.

- **A "now calling" strip of live boards.** Cheap: the directory already
  has each board's state and callers-on figure. Board-level only, because
  announce deliberately sends nothing about callers, so "The Rusty Antenna,
  3 of 10 on" and never handles. Strong as proof, weak until there are
  enough boards: with two listed it advertises how few there are. Build it
  gated on a minimum (say eight boards up) and fall back to the stat line.
- **Guestbook / "last callers".** A per-person last-callers list is not
  possible and should not be (see above). A web guestbook means a form, spam
  and moderation, on a site with no accounts: expensive and off-brand ("No
  web"). The honest version is board-level: "last boards to come online" and
  "newest board", which the RSS feed already computes. Cheap, mildly
  shareable (a sysop likes seeing their board named).
- **Period ASCII and ANSI touches.** Cheap and it is what makes the page
  feel like a BBS rather than a SaaS landing page. The best one is real: put
  a genuine screen capture of the board's welcome screen, from the host build,
  in a terminal frame beside or under the hero (ANSI at 80 columns and PETSCII
  at 40, side by side, is the machine-agnostic statement in one picture).
  Second: a kicker styled as a connect string (`CONNECT 2400` or `ATDT`) is
  fun but reads as dial-up cosplay to anybody under forty; use it on /firstcall,
  not the hero. Avoid fake terminal chrome with invented content: the rule for
  the S3 panel (never a mock-up) should apply to every screen we show.
- **Sysop of the month (or "board of the month").** Nearly free in code (a
  block in pages/*.md), costs Rob an hour a month, and is **the strongest
  sharing driver on this list**: a featured sysop posts it to their own club,
  forum and socials, which is exactly the network we want to reach. Needs
  consent and a photo of their setup. Start as "board of the month" at launch
  and switch to a sysop interview once there are a dozen boards.
- **Also worth it: a "dial-in night".** Once a month, a date and a board
  (Unleashed HQ to start): call it from the oldest machine you have and post
  the photo with a tag. It fills the chat room, feeds the directory, and
  produces the community photos the site lacks. Costs a post and a date.

Cheapest first: ANSI/PETSCII capture beside the hero, `og:` fix, board of
the month. Then "now calling" once the count helps.

## 4. Photo shot list for Rob

General rules: real boards showing real screens (no mock-ups, nothing
composited); no IP addresses, passwords or personal handles legible, so use a
test board with a hostname like `unleashed.local` and a demo handle; a dark,
matte background close to the site's #0b0b0f so photos sit on the page
without a box; shoot landscape at 3:2 or wider, at least 2400 px, and keep
the subject central so the same file crops to 1:1 for social and 1200x630
for `og:image`.

1. **The hero shot: the board in a palm.** The ESP32 dev board on an open
   palm, activity LED lit, USB cable trailing off frame. 45 degrees above,
   shallow depth of field, the hand in soft light, the board sharp. This is
   the literal picture of the headline and goes beside it on the front page.
2. **Old and new in one room.** A vintage machine and a modern laptop side by
   side, both in the same chat room, the same line of chat visible on both
   screens (one in PETSCII or a period font, one in ANSI). Eye level, both
   screens readable. Use whichever vintage machine is to hand; the point is
   the pair, not the brand. The best `og:image` candidate and the lead
   social image.
3. **The S3 panel, live.** The Waveshare S3 in a USB port or charger, its
   screen showing a real caller list and the board's name. Macro, straight
   on, screen brightness turned down so it does not blow out. For /hardware
   and the "screen of its own" card.
4. **Lights in a case.** A dev board in an enclosure with the ten-pixel strip
   lit for several callers and the drive light on. Low ambient light, on a
   tripod, so the pixels glow without flaring. For /lights and social.
5. **The camera, both ends.** The Freenove board pointed at something worth a
   look (a bird feeder, a workbench), and in the same frame or as a pair, the
   downloaded SNAPSHOT open on a PC screen. For /camera and the 1.1.0
   announcement.
6. **The whole kit, flat lay.** Top-down on the dark surface: dev board, USB
   cable, micro SD card and module, a phone charger, with a coin or a ruler
   for scale. For /build and /hardware: it answers "what do I need" before
   the reader asks.
7. (Optional) **Minutes, proven.** A laptop showing the /install page mid-flash
   with the board plugged in beside it. For /install and the "built in minutes"
   card.

## 5. The Telnet BBS Guide

How a sysop lists a board, from the Guide's own FAQ
([How To Add Your BBS Listing](https://www.telnetbbsguide.com/faqs/how-to-add-your-bbs-listing/)):

- Create a user account on the Guide ("Sysop Register").
- Check the board is not already listed. If it is, contact the admins to have
  the listing linked to your account for edit rights.
- Otherwise use "Add Your BBS" to create a new listing. It does not appear
  until an administrator approves it.
- The Guide publishes an official list monthly for other websites and BBSes
  to redistribute, plus an unofficial daily one
  ([download lists](https://www.telnetbbsguide.com/lists/download-list/)),
  so one listing travels further than one site.

Not documented on the Guide's pages, so to confirm while doing the first
listing: which fields are required, whether its software list offers
"Other", and whether it checks or drops boards that stop answering.

What to say, as a template:

- Name: the board's name. Address: its hostname and port (a stable DDNS name,
  never a bare home IP that will change). Software: µnleashed BBS (ASCII
  "Unleashed BBS"). Access: Telnet.
- Brief: "A µnleashed BBS on an ESP32: chat, mail and forums. PETSCII, ANSI
  and ASCII detected at connect."
- Detailed: what the board is about first (the club, the topic), the
  software second.

**Rob, once, as the author:** ask the admins through Contact Us to add
µnleashed to their software list. A software name gets its own filter page
there (Synchronet has 298 boards under its name, Mystic 190), and that page
is permanent visibility for the project with every board that joins it.

**Should the directory or the board help?**

- Directory: yes, cheaply. A short "Get listed elsewhere too" section on
  /how with the steps above and the template, pre-filled on each board's own
  row where possible, so a sysop can copy it. Remind them of the same rules
  as our listing: change the default password and forward the port first.
- Firmware: no. The Guide is human-moderated with no documented API, and an
  automated submission to somebody else's moderated list would be rude and
  brittle. Nothing in announce should talk to it.

## 6. Round 2 mockups

Same method as round 1 (the live page's own HTML and CSS, Oxanium
self-hosted, only the opening row replaced, empty database). In the session
scratchpad, `marketing/round2/`:

- `recommended-1366.png`, `recommended-390.png`: the fixed hero, "Build a
  community on a chip that fits in your palm.", with the server-rotated
  audience kicker showing "For a retro computing club".
- `rot-frame-1-1366.png` to `rot-frame-4-1366.png`, and the same at 390: the
  CSS-cycle fallback, one frame per line (Build a community / Your people.
  Your rules. Your board. / Start a place people want to call / No platform.
  No feed. Just your people.), with the progress pips and the Pause control.
- `rotating-live.html` is the working CSS-only cycle (six seconds a line,
  pause by checkbox, still under reduced motion), to open in a browser;
  `build_r2.py` builds everything.

---

# Round 3 (same day): welcoming, for people who never heard of a BBS

Rob: "'Your People' sounds so off-putting ... 'Your Community' sounds much
better. Remember we use 'Board' because it makes sense to people who are
old, but if I asked a 20-something what a BBS was they would look at me
like I was on drugs ... we're trying to engage people who are technical and
non-technical, retro but not retro too. Like leveraging the social media
before it was social media ... the list of boards can move to its own page
... overall we need to clean up all the site language so it's more welcoming
to non-technical."

Agreed on every point, and "your people" was my mistake: it reads as
tribal, and "your community" says the same thing warmly. Round 2's lines
withdrawn: "Your people. Your rules. Your board." and "No platform. No feed.
Just your people."

The rule for this round: **say what it is in today's words first (your own
community space online, on a tiny device you own), then the hook (this is
how people met online before social media), then the proof.** "BBS" and
"board" appear after the reader knows what the thing is, as the explanation
and the nostalgia, never as the headline.

## 1. Hero lines

Recommended first. Every line is true of the $5 dev board with no card
(chat and messages); anything needing a card says so.

- **Your own online community, on a device that fits in your hand.**
  Kicker above it: **Social, before social media.**
  - Sub: A place for your class, club, family or friends to chat and leave
    each other messages, running on a $5 device you own. No ads, no feed, no
    platform in the middle. It is how people met online before social media,
    and you can set one up in about five minutes.
  - Why first: it answers "what is it?" in words a 20-year-old and a
    70-year-old both already use (online community, device), the kicker
    delivers Rob's hook in four words, and "fits in your hand" is the
    concrete, surprising part. It works for a technical reader too, because
    the sub goes straight to $5 and five minutes.
- **Social, before it was social media. Now you can run one.**
  - Sub: Before the apps, people met online on bulletin boards run by somebody
    they knew. µnleashed brings that back on a $5 device: your community, your
    rules, no ads and no feed. Set it up in about five minutes.
  - The strongest nostalgia line; it leans on the reader knowing there was a
    "before", so it is a better social post or /different opener than hero.
- **A community space you own, not one you rent.**
  - Sub: Chat and messages for your club, class or family on a $5 device at
    home. Nobody else's servers, nobody else's rules, and nothing to pay each
    month.
- **Start your community's own corner of the internet.**
  - Sub: A chat room and messages for up to ten people at once, on a device
    the size of a stick of gum. Set it up from your browser in about five
    minutes, then share the address.
- **The online hangout you run yourself.**
  - Sub: For a class, a club, a family or a group of friends: a chat room,
    messages, and forums once you add a memory card. It runs on a $5 device,
    and people join from a laptop, a phone, or a computer from the 1980s.
- **Build a community on a chip that fits in your palm.** (Rob's round 2 seed)
  - Judged honestly against a non-technical reader: it does not survive as
    the hero. "Chip" is maker vocabulary (and to some readers, food), and
    "build" sounds like work before the reader knows it takes five minutes.
    It is a very good line for the makers: keep it for /build, /hardware and
    posts to maker and Hackaday audiences.

## 2. Page structure, and the mockup

The front page becomes the pitch; the live list moves to /directory.

1. **Hero:** kicker, headline, sub, two buttons (**Build yours** to /install,
   **Try one first** to /directory), a facts line (About $5 · About five
   minutes · No subscription · Free software), and beside it a picture of the
   board (a line drawing in the mockup, to be replaced by shot 1).
   "Run your own board" folds into these buttons and into the closing band;
   "Build from source" moves to a one-line developer link at the foot.
2. **What it is:** one sentence (a chat room, messages, and with a memory
   card, forums and a file library; up to ten people at once) and three
   cards: Yours, not a platform's · Everyone can join · Tiny, and always on.
3. **Who builds one:** six cards, each headed **For example**, each a
   situation, not a person: a class, a ham radio club, a retro computing
   group, a family, a street or a building, a group of friends. The lead says
   so: "A few examples of what a community on a board could be. When real ones
   write in, their stories go here." No quotes, no names, no testimonials.
   Rob's teacher idea is the first card, as a possibility: "A teacher could
   have her class's community running before the bell."
4. **Three steps:** Get a board (about $5, which one to buy) · Install it from
   your browser (USB cable, Chrome or Edge, pick your Wi-Fi, about five
   minutes) · Invite your community (share the address; people join with a
   free app on their computer or phone; how joining works).
5. **Before social media, there were bulletin boards:** the nostalgia strip.
   A paragraph (the 1980s and 90s, one person's computer and a phone line,
   an estimated 60,000 in the US at the peak, the host was somebody you could
   talk to) and a small terminal-style timeline: 1978 the first one goes
   online in Chicago; the 1990s, about 60,000 in the US; today, one fits in
   your hand. All from README.md's sourced history (CBBS, 16 February 1978,
   and Wikipedia's peak estimate). No invented board screen: a real capture
   can sit beside it later.
6. **Closing band:** "See one, then start yours", the two buttons again, and
   the developer line (build from source, GitHub).
7. Footer as today, minus the list's three explanatory lines, which move to
   /directory with the list.

Nav, for explain to settle: "BOARDS" should become **Find a community**
(to /directory), with the wordmark taking you home. The masthead ticker's
first item, "No web: a BBS, not a website", reads as a contradiction to a
newcomer standing on a website; see the audit.

Mockup files, in the session scratchpad, `marketing/round3/`:
`home-1366.png` (full page), `home-390.png` (full page), `home.html`, and
`build_r3.py`, which builds them from the live page's own head, nav and
footer.

## 3. Language audit for non-technical readers

The pattern across the site: it was written by and for somebody who already
runs a BBS, so it uses the hobby's words (caller, dial, sysop, handle,
terminal, flash) without saying what they are. None of those words is
wrong; each needs either a plain replacement or one line of explanation the
first time it appears on a page. Keep the period words where they are
charm (the /firstcall art, the nostalgia strip), and replace them where they
are instructions.

### Words used across the site

| Now | Plain | Note |
|---|---|---|
| BBS, board (as a headline or first mention) | "your community space" / "a community board"; then "(a BBS, or bulletin board)" once | "board" is fine after it has been explained |
| caller | "visitor", "member", or "someone who connects" | keep "caller" in the nostalgia copy |
| dial, dial in, call | "connect", "join", "visit" | "call in" can stay as flavour once explained |
| sysop | "host", or "the person who runs it (the sysop)" first time | |
| handle | "nickname" | /firstcall already explains it; lead with the plain word |
| telnet client, terminal | "a free app for connecting", with /terminals as "Apps for joining" | keep "telnet" in technical sections |
| flash, firmware, image | "install", "the software", "the version for your board" | |
| ESP32 dev board (Base) | "the $5 ESP32 board (a tiny Wi-Fi computer)" | /build already has the right sentence: "A small computer with Wi-Fi built in" |
| SD card module, file areas | "a memory card add-on (about $2)", "a shared file library" | |
| port, serial port (installer) | "the board's USB connection"; "the list of connected devices" | |
| Go public, forward a port | "Open it to the internet" / "Let people outside your home in" | |
| Get listed, directory | "List your community", "Find a community" | |
| PETSCII, ANSI, CP437, UTF-8, ASCII | "whatever computer or app someone joins from, it looks right" | keep the list in /terminals and /firstcall's troubleshooting |
| XMODEM, YMODEM | "files people can download" | keep in the technical line |
| toolchain | "programming tools" | |
| PSRAM, SRAM, 4 MB flash, WROOM | move under a "Technical details" heading | |

### Page by page (newcomer pages first)

**Home (server.py strings)**
- Pitch "A whole BBS on a board the size of a stick of gum ..." → the round 3
  hero.
- Run card "Run your own board / An ESP32 board, a USB cable, five minutes /
  Web installer / Build from source" → folded into "Build yours" and "Try one
  first"; "Build from source" becomes a footer line for developers.
- Ticker "No web: a BBS, not a website" → "No ads: nothing is selling your
  attention". "No browser: a 1980s computer can call in" → "Old and new: a
  laptop or a 1980s computer can join". "GPL v3 or later: free software" →
  "Free software: yours to read and change". "You write the rules: and you are
  the appeal" → "Your rules: you run it, you decide". "Run your own directory:
  this one is free software" → move off the ticker; it matters to few.
- List lead "Dial one with any telnet client, or click an address if you have
  one installed. Nothing happened? Never called one before?" (moving to
  /directory) → "Pick one and connect. First time? You need a free app, and it
  takes a minute: [how to join]. [Didn't connect?]"
- Heading "BBS directory" (on /directory) → "Communities running right now".
  Stat "Unleashed is hosting 3 boards with 5 callers on right now" → "3
  communities online, 5 people connected right now."

**/whofor**
- Title "Who it's for" → "Who builds one". Opening "Everyone. That is not a
  dodge, it is the answer" is good; keep.
- "It is the internet with the lid off. A caller opens a socket to an address
  and a port" → "It shows how the internet works underneath: a student
  connects to an address, types, and the words come back."
- "One board takes ten callers at once" → "Ten people can be on at once".
- "sysop's caller log" → "the host's visitor log".
- "You flash it, you give it your Wi-Fi, and it answers. Forward one port and"
  → "You install it, give it your Wi-Fi, and it is ready. Change one setting on
  your router and people outside your home can join too."

**/different**
- Title "What makes it different" → "What a board can do".
- "No computer under it" → "No computer needed". "no operating system to keep
  patched" → "nothing to keep updating".
- "One port for every terminal ... ANSI, UTF-8, PETSCII at 40 or 80 columns or
  plain ASCII" → "Works with any computer, old or new: it works out what each
  visitor is using and draws the screen to fit."
- "a lifetime of disk images, text files and utilities ... XMODEM or YMODEM" →
  "a shared library of files people can download, even to computers from the
  1980s."
- "a caller ringing for the sysop" → "someone asking for the host".
- "µnleashed is firmware: the $5 board is the whole computer" → "µnleashed is
  the software on the board, and the $5 board is the whole computer."

**/install**
- Title "Put the BBS on your board" → "Set up your board".
- "Close anything else using the serial port" → "Close any other program
  that is connected to the board".
- "The browser then lists the serial ports it can see" → "The browser lists
  the devices plugged in".
- "Telnet details" is the installer's own button label (vendored); explain it
  once: "**Telnet details** is how to connect: it shows your board's address."
- Keep the troubleshooting precise; only the first screen needs the plain
  pass.

**/hardware**
- Title "Tested boards" → "Which board to buy". Lead with the three choices
  in plain words (the page already has them: "functional, and the lowest
  cost" ...) before any chip names.
- "A dev board round the ESP32-WROOM-32E module, with 4 MB of flash and a
  USB-serial chip" → "The small board most shops sell as an 'ESP32 DevKit'",
  and move the module detail under "Technical details".
- "ten caller lines, a busy line and a hidden sysop line. 520 KB of SRAM and 4
  MB of flash, no PSRAM" → "Up to ten people at once"; the rest under
  "Technical details".
- "What it does with no card: chat, mail, accounts, the information pages and
  a directory listing" → "Without a memory card: chat, messages, accounts and
  information pages."

**/build**
- "A board of your own, on hardware that costs less than lunch" is good.
- "and no toolchain" → "and no programming tools".
- "If you have an ESP32 dev board in a drawer" → keep; it is the maker hook.
- "Getting it running: You need PlatformIO and git" → put under a heading
  "For developers: build from source", after the installer route.
- "Coming soon, two camera boards add a third: ... from any terminal" → "from
  any computer that connects".

### The ten fixes that matter most

1. Home hero: the round 3 line, "Your own online community, on a device that
   fits in your hand."
2. "caller" → "visitor" or "member" in instructions.
3. "dial / call a board" → "connect to / join a community".
4. "telnet client" → "a free app for joining", and /terminals renamed "Apps
   for joining".
5. "sysop" → "host" (with "sysop" explained once).
6. "BBS directory" / "Boards" in the nav → "Find a community".
7. "Go public" / "forward a port" → "Let people outside your home join".
8. "flash / firmware / image" → "install / the software".
9. "PETSCII, ANSI, CP437, UTF-8" → "it looks right on whatever they use"
   (the list moves to the technical pages).
10. Ticker "No web: a BBS, not a website" → "No ads: nothing is selling your
    attention".

**The one thing copy cannot fix, and it matters most for non-technical
readers:** joining a board needs a terminal app. A phone needs one from the
app store, Windows has its telnet client switched off by default, and
nothing opens in the browser. "Try one first" sends a newcomer to /directory,
and the next thing they meet must be one friendly step: "You need a free app.
On a phone: TERMinator (Android and iPhone). On a computer: SyncTERM." (the
choices README.md already recommends). Browser access through a public
WebSocket proxy is in the firmware's queue as an experiment and is not built;
it would be the biggest single lever for non-technical visitors, with the
trust caveat already written there (the proxy sees the traffic).

## 4. Link previews, specified

Today every page shares the same square logo avatar (1024x1024, shown as a
small thumbnail by most platforms), and the front page's `og:description` is
the board count ("0 bulletin boards listed, 0 up right now ..."). Somebody
sharing the site shares a number.

Add to every page: `og:image` at 1200x630, `og:image:width`/`height`,
`og:image:alt`, and `twitter:card = summary_large_image` so the large card
is used wherever that tag is read.

| Page | og:title | og:description | og:image |
|---|---|---|---|
| / | µnleashed: your own online community, on a device that fits in your hand | Chat and messages for your class, club, family or friends, on a $5 device you own. No ads, no feed. Set it up from your browser in about five minutes. | the branded card now; shot 2 (old and new in one room) once taken |
| /directory | Communities running right now | {n} communities online and {m} people connected right now. Visit one with a free app on your computer or phone. (The live figures belong here, not on the home page.) | branded card |
| /whofor | Who builds one | Classes, clubs, families, neighbourhoods and friends: what a community of your own on a $5 device can look like. | shot 1 (board in a palm) |
| /different | What a µnleashed board can do | No computer needed, installed from your browser, works with any computer old or new, and lights, a screen and a camera if you want them. | shot 4 (lights) |
| /install | Set up your board from your browser | Plug in a $5 ESP32 board, press Install in Chrome or Edge, and pick your Wi-Fi. About five minutes, no programming. | shot 7 (installer) or branded card |
| /hardware | Which board to buy | Three tested boards, from about $5 to about $20. Pick by the picture; each installs from your browser. | shot 6 (the kit, flat lay) |
| /build | Build your own community board | Everything you need for a board of your own, and how to add a memory card, lights and more. | shot 6 |
| /firstcall | Joining a board for the first time | What happens when you connect: pick a nickname, register or come in as a guest, and say hello. | branded card |

`og:image:alt` for the branded card: "The µnleashed wordmark and the words
'Your own online community, on a device that fits in your hand', beside a
drawing of the board."

The branded card is in the scratchpad: `marketing/round3/og-card-1200x630.png`
(and `og-card.html`). It uses the site's wordmark, Oxanium and palette, and
the same line drawing of the board, and is text-safe for the square crops some
platforms take (the headline sits left of centre, the URL bottom left).

## 5. The Telnet BBS Guide, for /how (draft for explain)

> ### List your community on the Telnet BBS Guide too
>
> The [Telnet BBS Guide](https://www.telnetbbsguide.com/) has listed
> bulletin boards for more than twenty years, and it is where many people
> who enjoy them look for somewhere new to visit. A listing there is free,
> and an administrator approves each one, so it may not appear straight
> away.
>
> 1. Change the sysop password and open your board to the internet first,
>    the same as before listing here.
> 2. [Create an account](https://www.telnetbbsguide.com/) on the Guide.
> 3. Check your board is not already there. If it is, ask the Guide's
>    admins to link it to your account.
> 4. Choose **Add Your BBS** and fill it in: your board's name, its address
>    and port (use a name that stays the same, not a home address that
>    changes), and µnleashed BBS as the software.
> 5. Say what your community is about first, and the software second.
>
> The Guide sends its list out every month to other websites and boards, so
> one listing travels a long way.

(Twenty years: the Guide ran its monthly list on Yahoo Groups for 19 years
before moving to Groups.io in February 2019. The FAQ it links to is
[How To Add Your BBS Listing](https://www.telnetbbsguide.com/faqs/how-to-add-your-bbs-listing/).
Still to confirm on the first real listing: the required fields, and
whether "µnleashed" is on its software list yet; Rob asks the admins to add
it once.)

## 6. Board of the month

Parked for a later version: with a handful of boards listed it would
feature the same few every month. Revisit once there are a dozen or more.
It stays the strongest sharing idea on the list.
