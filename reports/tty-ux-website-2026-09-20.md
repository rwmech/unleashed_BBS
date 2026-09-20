# UX review: the public website

Date: 2026-09-20
Reviewer: tty-ux (design consultant)
Subject: `unleashed_directory` at `C:\Users\rwmec\Documents\Development\BBS\unleashed_directory`
Scope: layout, hierarchy, navigation, width, rhythm, responsive behaviour, the
board listing as a product, consistency with the board.

Out of scope by instruction, and not covered here: prose, voice, wording,
factual accuracy, and the blockquote/callout left-indent bug.

## How this was measured

Nothing below is read off the Markdown or the source. The server was run on
`127.0.0.1:8731` against a scratch database seeded with eight boards covering
the awkward cases (a 47 character board name, a 44 character hostname, a board
with no description, a board with no activity figures, two offline boards).
Every page was fetched over HTTP, saved, and rendered in headless Chrome at
1920x1080, 1366x768 and a true 390x844. Boxes, column widths, character cells
and document scroll width were read out of the live layout with
`getBoundingClientRect`, not estimated.

Headless Chrome on Windows will not open a window narrower than about 500 CSS
px, so 390 was obtained by putting the page in a 390px iframe inside a wide
window and lifting the child's measurements out. Widths quoted as `ch` are
measured cells: **1 ch = 7.7 px** at the site's 14px monospace, so the 1080px
content column is exactly **140 characters**.

Every proposed rule in this report was applied as an override to a scratch copy
of the served HTML and re-rendered at all three widths. The numbers in the
"after" mock-ups are measured from those renders, not predicted.

No traffic was sent to any live host.

---

## The verdict

This is a good site wearing a layout that was written once at desktop width and
never measured again, and the single worst consequence is that the product does
not work on a phone at all: the board table is 627px of content in a 358px
column, so a visitor on a phone gets page-level horizontal scroll and cannot
see the State, Activity or Up-for columns without dragging the whole page
sideways, which means the one question the directory exists to answer, is this
board up and is anybody on it, is off screen. Nothing else here is that bad.
The rest is a set of decisions that were each individually reasonable and were
never re-checked against each other: prose set at 140 characters because a
float on one page needed room, a page title rendered smaller than the body text
it introduces, a State column given 10 characters to hold a 14 character
figure, and seven of twelve pages sharing one browser tab title. All of it is
fixable in CSS plus about twenty lines of Python, none of it needs a redesign,
and the parts that were designed on purpose (the wordmark, the colour
semantics, the 1080px cap, refusing JavaScript) are right and should be left
alone.

---

## Findings, worst first

### 1. The board list does not fit a phone, and the part that gets cut is the product

- Where: `server.py:875` (`table {{ border-collapse:collapse; width:100%; }}`),
  the index table markup at `server.py:1215-1217` and `board_rows` at
  `server.py:1117-1177`. There is no media query anywhere that touches it; the
  only one in the file is `server.py:938` and it handles a floated photograph.
- Breaks at: **any viewport narrower than 659px.** Measured: the six column
  table has a minimum content width of 627px, plus 32px of body padding.
- Measured at 390: `document.scrollWidth` 643 against `clientWidth` 390. The
  page scrolls sideways by 253px, which is 65% of the screen.

Now, at 390 (46 characters of usable column, table forced to 81):

```
col 1        10        20        30        40    46|          the table carries on to column 81
    |........|.........|.........|.........|.....|
    Board          Dial             Sysop   | State     Activity      Up for
    Node Zero /    sector7g.long-   Will... |
    Sector 7G      hostname-for-    Ashcr.. |   <- everything right of this line
    Telecommunic   testing.exampl   Banner. |      is off the screen
    ations
    Exchange
     Synchronet
     3.20
    A deliberately
    long name and
    a deliberately
    ...
```

The board name wraps to seven lines in a 20 character column. The description
wraps to eleven. The state, the activity and the uptime are not visible. The
first board row is 373px tall and starts at y=346, so on a 390x844 phone with
browser chrome a visitor sees one board, partially, and cannot tell whether it
is up.

After, same 46 columns, measured from the render:

```
col 1        10        20        30        40    46
    |........|.........|.........|.........|.....|
    Node Zero / Sector 7G            11 of 16 on
    Telecommunications Exchange                1m
     Synchronet 3.20
    A deliberately long name and a
    deliberately long description, to see
    what the table does when somebody
    actually fills in every field.
    ▁▂▃▅▇▇▅▂ busiest 22:00-00:00
    sector7g.long-hostname-for-testing.example.org
    sysop Wilhelmina Ashcroft-Bannerman
    24h 233 calls, 67h 01m connected
    up for 140d
    ------------------------------------------
    unleashed HQ  unleashed 0.17.1    3 of 10 on
                                             now
    ...
```

Measured after: `scrollWidth` 390, `clientWidth` 390, zero overflowing
elements, and six boards visible in the first 2000px instead of one truncated
one.

**The rule.** Below 900px the table stops being a table and becomes a list of
boards, one flex column per row, with the state pinned top right where a
scanner looks for it. The breakpoint is 900 and not 700 because the fixed
columns in finding 2 add up to 82 characters, and below about 900px the Board
column is squeezed under 21 characters.

```css
@media (max-width: 900px) {
  main > table, main > table > tbody { display: block; }
  main > table tr { display: flex; flex-direction: column; position: relative;
                    padding: 14px 0 16px; border-bottom: 1px solid var(--rule); }
  main > table tr:first-child { display: none; }          /* the header row */
  main > table td { display: block; border: 0; padding: 1px 0; width: auto; }
  main > table td:nth-child(1) { order: 1; padding-right: 16ch; }   /* board  */
  main > table td:nth-child(4) { order: 2; position: absolute; right: 0;
                                 top: 14px; width: 15ch; text-align: right; }
  main > table td:nth-child(2) { order: 3; margin-top: 6px; }       /* dial   */
  main > table td:nth-child(3) { order: 4; }                        /* sysop  */
  main > table td:nth-child(5) { order: 5; }                        /* 24h    */
  main > table td:nth-child(6) { order: 6; }                        /* up for */
  main > table td:nth-child(3),
  main > table td:nth-child(5),
  main > table td:nth-child(6) { font-size: 12px; }
  main > table td:nth-child(3)::before { content: "sysop ";  color: var(--faint); }
  main > table td:nth-child(5)::before { content: "24h ";    color: var(--faint); }
  main > table td:nth-child(6)::before { content: "up for "; color: var(--faint); }
  .addr a { padding: 8px 0; }
}
```

`15ch` for the state box is not arbitrary: `quiet, 7 h ago` is 14 characters
and `11 of 16 on 1m` is 14. At 13ch both wrap and the number separates from its
unit, which was checked and then corrected during this review.

The `::before` labels are the cheap version. The honest version is one
`data-label` attribute per `<td>` in `board_rows` (`server.py:1158-1176`) and
`content: attr(data-label)`, which costs six short strings and stops the
labels being tied to column order.

---

### 2. The desktop table has no column budget, so figures split across lines

- Where: `server.py:875-877`. `table-layout` is left at `auto`, so every column
  width is negotiated from whatever text eight arbitrary boards happen to
  carry. Add a board with a long sysop name and every column on the page moves.
- Breaks at: **every width**, including 1920.
- Measured at 1366 and 1920 (main is capped at 1080px so both are identical):
  Board 454px/59ch, Dial 243px/32ch, Sysop 131px/17ch, **State 78px/10ch**,
  Activity 123px/16ch, **Up for 51px/7ch**.

State gets 10 characters and has to hold 14. Up-for gets 7 characters minus
16px of padding, which is 4.5, and has to hold the word `Up for`.

Now, at 140 columns:

```
col 1        10        20        30        40        50        60        70        80        90       100       110       120       130     140
    |........|.........|.........|.........|.........|.........|.........|.........|.........|.........|.........|.........|.........|......|
    Board                                                      Dial                            Sysop            State     Activity        Up
                                                                                                                                          for
    Node Zero / Sector 7G Telecommunications                   sector7g.long-hostname-for-     Wilhelmina       11 of 16  233 calls,      140d
    Exchange  Synchronet 3.20                                  testing.example.org 2323        Ashcroft-        on 1m     67h 01m
    A deliberately long name and a deliberately                                                Bannerman                  connected
    long description, to see what the table
    ...
    Hollow Moon  unleashed 0.13.0                              hollowmoon.example.org 6400     Dex              quiet, 7  5 calls, 44m     31d
    Message bases, no files. 300 baud spiritually.                                                              h ago     connected
```

Three separate failures in one screenshot: the `Up for` header wraps onto two
lines, `quiet, 7` is separated from `h ago`, and `67h` is separated from
`01m`. A figure split across a line break is not a figure, it is two numbers.

After, measured:

```
col 1        10        20        30        40        50        60        70        80        90       100       110       120       130     140
    |........|.........|.........|.........|.........|.........|.........|.........|.........|.........|.........|.........|.........|......|
    Board                                           Dial                    Sysop          State            Activity           Up for
    Node Zero / Sector 7G Telecommunications        sector7g.long-hostname- Wilhelmina     11 of 16 on 1m   233 calls          140d
    Exchange  Synchronet 3.20                       for-testing.example.org Ashcroft-                       67h 01m connected
    A deliberately long name and a deliberately     2323                    Bannerman
    long description, to see what the table does
    ...
    Hollow Moon  unleashed 0.13.0                   hollowmoon.example.org  Dex            quiet, 7 h ago   5 calls            31d
    Message bases, no files. 300 baud spiritually.  6400                                                    44m connected
```

**The rule.** Fix the five columns that have a known shape in characters, and
let the Board column absorb whatever is left. This is the same discipline the
board screens already use and it is the right model for a monospace layout.

```css
@media (min-width: 901px) {
  main > table { table-layout: fixed; }
  main > table th:nth-child(1), main > table td:nth-child(1) { width: auto; }
  main > table th:nth-child(2), main > table td:nth-child(2) { width: 24ch; }
  main > table th:nth-child(3), main > table td:nth-child(3) { width: 14ch; }
  main > table th:nth-child(4), main > table td:nth-child(4) { width: 17ch;
                                                               white-space: nowrap; }
  main > table th:nth-child(5), main > table td:nth-child(5) { width: 19ch; }
  main > table th:nth-child(6), main > table td:nth-child(6) { width:  8ch;
                                                               white-space: nowrap; }
}
.addr a { overflow-wrap: anywhere; }
```

Measured result, Board column: 48ch at 1366 and 1920, 31ch at 1000, 21ch at
920. Row heights settle at 163/100/100/100 instead of 163/100/100/79.

Two supporting changes, both in `board_rows` at `server.py:1146-1156`:

- Put a `<br>` between the call count and the connected time, so `Activity` is
  two deliberate lines (`233 calls` at 9ch, `67h 01m connected` at 17ch) rather
  than one line wrapping wherever it lands. 19ch then always holds it.
- `where` and `port` are joined with a space in one link. Keep that, it is
  right, but the `overflow-wrap: anywhere` above is what stops a 44 character
  hostname dictating the whole table's geometry.

---

### 3. Prose is set at 140 characters

- Where: `server.py:915` (`article {{ max-width:none; }}`) and `server.py:919`
  (`article p, article li, article dd {{ max-width:none; line-height:1.62; }}`).
- Breaks at: **1366 and above**, where `main` reaches its 1080px cap.
- Measured: every paragraph on `/build`, `/dialing`, `/forward`, `/terminals`,
  `/sdcard` and the about page is **140 characters wide**. Monospace prose
  stops being comfortable somewhere around 90.

The comment above line 919 justifies it: a floated picture needs text on both
sides rather than a column that stops before it starts. That is true of exactly
one paragraph block on one page. The other five pages have no float at all and
pay the cost anyway.

It also does not work. The float is `width: min(34%, 380px)`, measured at 367px
in a 1080px column. A paragraph capped at 78ch is 600px. 600 + 367 + 30px of
margin is 997, which fits inside 1080, so the float still has text beside it.
This was rendered and checked, not reasoned about.

```css
main p, main li, main dd { max-width: 78ch; }
article figure, article .wide, article pre,
.tablewrap, main > table { max-width: none; }
```

Use `main p`, not `article p`. The lead paragraph and the byline on the about
page sit **outside** the `<article>` element (`server.py:1461-1467` puts `lead`,
`byline` and the animated diagram before the `<article>` opens), so the current
`article p` selector already misses them and they run at 140ch while the body
beside them runs at 78. Half-applied is worse than not applied.

Related, same family: `pre` at `server.py:968` has no measure either, so a six
line shell snippet sits in a 140 character box next to 78 character prose.
`article pre { max-width: 92ch; }` puts code slightly wider than prose, which
is the correct relationship, and was rendered to confirm it.

---

### 4. The hierarchy is inverted: the page title is smaller than the text it introduces

- Where: `server.py:870` (h1), `server.py:946` (article h2), `server.py:947`
  (article h3), `server.py:830` (body).
- Breaks at: **every width**.
- Measured type scale, as computed styles:

| Element | Size | Colour | Treatment |
|---|---|---|---|
| `article h2` | **15px** | `--struct` cyan | normal case |
| `body`, `p.lead` | 14px | `--ink` / `--dim` | |
| `h1` | **13px** | `--ink` | uppercase, letter-spacing 3px |
| `article h3` | **13px** | `--ink` | uppercase, letter-spacing 1px |
| `nav a` | 12px | `--dim` | uppercase, letter-spacing 1px |

Two things are wrong and they compound. The `h2` is larger than the `h1`, so on
`/build` the section heading `What you need` outranks the page title
`BUILD ONE`. And `h1` and `h3` are the same size, the same colour and both
uppercase with letter-spacing, so `COMMODORE` four screens down the terminals
page is indistinguishable from the page's own title.

On the index this costs the most, because the h1 is where the live figures
live: `BBS DIRECTORY · 8 listed · 15 callers on`. The number that makes the
site worth reloading is set in 13px `--faint`, which is the smallest and
faintest text above the fold.

```css
h1      { font-size: 20px; letter-spacing: 2px; margin: 0 0 6px; }
h1 span { font-size: 13px; letter-spacing: 0; color: var(--dim); }
article h3 { color: var(--dim); }              /* below h2, not level with h1 */
```

Rendered and checked at 1366 and 390. `20px` was picked as the smallest size
that visibly outranks the 15px h2 without breaking the terminal feel; anything
above about 24px starts competing with the wordmark.

Worth doing at the same time, in `index_page` at `server.py:1210`: take the
caller count out of the `<span>` and give it `--live`, the colour that already
means "up" everywhere else on the page. The figure is the product.

---

### 5. Seven of twelve pages share one browser tab title, and the flagship page has no `<h1>`

- Where: `server.py:565` (`md_page` passes `title=html.escape(SITE_NAME)` for
  every Markdown page) and `server.py:1461` (`ABOUT` opens with `p.lead`, never
  an `h1`).
- Breaks at: every width, and in every tab, bookmark, history entry and search
  result.

Measured, as served:

| Path | `<title>` | `<h1>` |
|---|---|---|
| `/` (list) | µnleashed BBS directory | BBS directory · 8 listed · … |
| `/` (about) | unleashed | **none** |
| `/` (data) | Data | Data |
| `/build` | **µnleashed BBS directory** | Build one |
| `/terminals` | **µnleashed BBS directory** | Terminal software |
| `/forward` | **µnleashed BBS directory** | Putting a board on the internet |
| `/dialing` | **µnleashed BBS directory** | Making the dial links work |
| `/sdcard` | **µnleashed BBS directory** | Adding an SD card |
| `/forward-asus` | **µnleashed BBS directory** | Port forwarding on an ASUS router |
| `/forward-mesh` | **µnleashed BBS directory** | Port forwarding on eero and Google Nest |
| `/how` | How to get listed | How to get listed |
| `/rules` | House rules | House rules |

Every Markdown page already carries its own title in its first `# ` line and
throws it away. Open four router pages in four tabs and they are four
identical, unreadable tabs.

The rule, in `md_page`:

```python
body = md_render(f.read_text(encoding="utf-8"))
m = re.search(r"^# (.+)$", f.read_text(encoding="utf-8"), re.M)
title = f"{m.group(1)} - {SITE_NAME}" if m else SITE_NAME
```

Page name first, site name second, because a tab strip truncates from the
right.

The about page needs an `h1`. It is the page the whole argument lives on and it
currently starts at `h2`, which means it has no document outline, no heading
for a screen reader to land on, and nothing on screen that says what the page
is called.

---

### 6. Two pages light up the wrong nav item, four light up nothing

- Where: `server.py:218-229` (`nav_html`), `server.py:1774-1779` (`simple_page`
  defaults `here=""`), `server.py:1838-1841` (the `/rules` and `/how` routes
  pass no `here`).
- Breaks at: every width.

Measured, which nav item carries `class="here"` as served:

| Path | marked current |
|---|---|
| `/rules` | **Boards** (wrong) |
| `/how` | **Boards** (wrong, and `Get listed` is right there in the nav pointing at `/how`) |
| `/dialing` | nothing |
| `/sdcard` | nothing |
| `/forward-asus` | nothing |
| `/forward-mesh` | nothing |

The cause is the fallback at `server.py:221-222`: `here in ("", "/")` is
treated as the index, so any page that forgets to declare itself claims to be
the board list. That is worse than no marker, because the nav is actively
lying about where the visitor is, on a site whose whole nav design is built
around reverse-video "you are here".

Two fixes, both small:

- Pass `here` from the routes: `simple_page("House rules", RULES, here="/rules")`
  and `simple_page("How to get listed", HOW, here="/how")`. `/how` then matches
  the existing `Get listed` entry.
- Drop the `here in ("", "/")` fallback and have the `/` handlers pass
  `here="/"` explicitly. An unmarked nav is honest; a wrongly marked one is not.

The four orphan pages (`/dialing`, `/sdcard`, `/forward-asus`,
`/forward-mesh`, and the three other router pages) are all children of a
section that is in the nav. Mark the parent: `/dialing` should light
`Terminals`, the four router pages should light `Go public`, `/sdcard` should
light `Build one`. One dict, seven entries, and every page in the site then
answers "where am I".

---

### 7. Contrast: the footer and every small annotation fail AA

- Where: `server.py:914` (`footer {{ ... color:#555 }}`), and everything using
  `--faint` `#6a6a72`: `.soft` (886), `.fresh` (894), `.when` (902),
  `svg.hours text` (910), `details.chart .note` (911), `.gallery .credit`
  (945).
- Breaks at: every width.

Computed against the `#0b0b0f` background:

| Colour | Used for | Ratio | Size | AA needs |
|---|---|---|---|---|
| `#555555` | the whole footer, including the paragraph explaining what the figures mean | **2.6:1** | 14px | 4.5:1 |
| `#6a6a72` (`--faint`) | freshness, software badge, chart labels and notes | **3.7:1** | **11px** | 4.5:1 |
| `#55555f` | photo credits | 2.7:1 | 11px | 4.5:1 |
| `#8a8a8a` (`--dim`) | descriptions, lead | 5.7:1 | 14px | passes |
| `#c8c8c8` (`--ink`) | body | 11.7:1 | 14px | passes |

`--dim` already passes and already reads as quiet. The fix is to stop going
below it for anything that is words.

```css
footer { color: var(--dim); }
.fresh, .soft, .when, details.chart .note,
.gallery figcaption, .gallery .credit { color: var(--dim); }
```

Keep `--faint` for rules, hairlines and the `::before` field labels, where it
is doing structural work rather than carrying text. The 11px sizes are worth
raising to 12px in the same pass; 11px monospace at 3.7:1 is not a size and a
contrast anybody reads, it is a size and a contrast that says "ignore this",
and some of what it is saying (how old the reading is) is exactly what the
footer paragraph spends four lines insisting matters.

---

### 8. Nothing on the site is a touch target

- Where: `server.py:855-857` (`nav a`), the `.addr a` link built at
  `server.py:1167-1170`, and the footer links at `server.py:244-249`.
- Breaks at: 390, and every phone.

Measured tap heights: `nav a` **26px** (12px text, 4px padding), `.addr a`
**21px** (no padding at all), footer links **21px**. Apple's minimum is 44px
and Material's is 48px.

`.addr a` is the primary action of the entire site. It is a 21px target and at
390 it wraps across three lines, which makes it three separate hit rectangles
none of which is a comfortable tap.

The footer is worse: at 390 the link text itself breaks mid-link, so
`House rules` renders as `House` on one line and `rules` on the next. A link
split across two lines looks like a rendering fault whether or not it is one.

```css
nav a    { padding: 11px 12px; }          /* 12 + 22 = 34px, with 6px gap = 40 */
footer a { display: inline-block; padding: 6px 0; }
.addr a  { display: inline-block; padding: 6px 0; overflow-wrap: anywhere; }
footer   { line-height: 1.7; }
```

Add `white-space: nowrap` to the footer links so a two-word link stays one
object, and let the run wrap between links instead of inside them.

---

### 9. The about page's animated diagram pushes the whole page sideways on a phone

- Where: `server.py:1394-1397`. `.scene pre` is `position:absolute` with
  `background:none; border:0; padding:0`, which deliberately unsets the global
  `pre` rule at `server.py:968` but does not unset its `overflow-x:auto`. An
  absolutely positioned box with no width shrink-wraps to content.
- Breaks at: **any viewport under about 520px.**
- Measured at 390: the art is 61 characters, so 470px, in a 358px column.
  `document.scrollWidth` 486 against `clientWidth` 390. The whole about page,
  header and footer included, scrolls sideways by 96px.

One line:

```css
.scene { overflow-x: auto; }
```

`.scene` is already `position:relative`, so it contains the absolute children
and the diagram scrolls within its own box while the page stops moving.
Measured after: `scrollWidth` 390, no page-level scroll.

---

### 10. The 60 second meta refresh throws away whatever the reader opened

- Where: `server.py:113-115` (`LIST_SECONDS`, `LIST_REFRESH`), applied at
  `server.py:1228`.
- Breaks at: every width, and hardest on a phone.

The day chart is a `<details>` element. A meta refresh is a document
navigation, so every open `<details>` closes and the reading position goes back
to the top. The interaction the page offers, click a sparkline and study a
board's day, is on a timer that cancels it after at most 60 seconds, and the
timer is invisible.

On a phone it also re-downloads a 60 KB document every minute for as long as
the tab is open.

The decision is argued in the comment and the argument is sound: a live list
should not go stale in a tab somebody left open. The cost was not counted. Two
ways through, in order of preference:

- Drop the meta refresh. Put the render time in the header next to the counts
  and let the reader decide. `BBS DIRECTORY · 8 listed · 15 callers on · as of
  21:14`. A visibly timestamped page is more honest than a silently refreshing
  one, and it fits the site's existing line about figures being only as fresh
  as the last heartbeat.
- Keep it and set `DIRECTORY_LIST_REFRESH` to 300. The list changes on a ten
  minute heartbeat, so a 60 second refresh is four times faster than the data
  it is refreshing.

---

### 11. The data page puts a one digit answer 705px away from its question

- Where: `server.py:875`, `table { width:100% }`, applied to the two column
  "Right now" table in `data_page`.
- Breaks at: 1366 and 1920.
- Measured: `State` column 571px (74ch), `Boards` column 509px (66ch), for
  content that is the word `offline` and the digit `2`.

```
col 1        10        20        30        40        50        60        70        80        90       100       110       120       130    140
    |........|.........|.........|.........|.........|.........|.........|.........|.........|.........|.........|.........|.........|.....|
    State                                                                          Boards
    offline                                                                        2
    online                                                                         6
```

A table exists to put a label next to its value. This one puts 74 characters of
nothing between them.

```css
article .tablewrap table, article table { width: auto; min-width: 0; }
article table th, article table td { padding-right: 3ch; }
```

`width:100%` is right for the board list, which is the product, and wrong for
every table inside an article. Scope it: move `width:100%` off the bare `table`
selector and onto `main > table`.

---

### 12. The hero photograph is four times too big and shifts the layout when it arrives

- Where: `gallery_html` at `server.py:590-637`, and `static/esp32.jpg`.
- Measured: the file is **1500 x 1196 and 487 KB**. It is displayed at 367 x
  420 with `object-fit: cover`. It carries `loading="lazy"` but no `width` or
  `height` attributes and the CSS sets no `aspect-ratio`, so nothing reserves
  its box and the paragraphs beside it jump when it loads.

Two fixes:

```css
.gallery img { aspect-ratio: 4 / 3; }        /* reserves the box before load */
```

and resize the source to 760px wide, which is 2x the largest slot it is ever
drawn in. That is roughly 487 KB down to 90 KB, on a site whose whole argument
is that things should be small.

The right long term answer is to read the JPEG header in `gallery_html` and
emit real `width`/`height` attributes, which is about a dozen lines and works
for whatever a sysop drops in `static/` later. The `aspect-ratio` rule is the
version to ship today.

---

### 13. The site has no description, no preview card and no icon

- Where: the `<head>` of `PAGE`, `server.py:817-820`.
- Measured: zero occurrences of `og:`, `twitter:`, `name="description"` or
  `rel="icon"` in any served page.

A BBS directory spreads by somebody pasting the link into a forum, a Discord or
a mailing list. Today that paste produces a bare link with no title card, and
because of finding 5 the link text is the same for seven different pages.

Four lines in the head, taking the same page title from finding 5:

```html
<meta name="description" content="{desc}">
<meta property="og:title" content="{title}">
<meta property="og:description" content="{desc}">
<meta property="og:type" content="website">
<link rel="icon" href="/static/favicon.svg">
```

On the index, `desc` should be generated: "8 boards listed, 6 up right now, 15
callers on." A preview card carrying a live number is the single best
advertisement this project has, and it is free.

The favicon should be the `µ` from the wordmark, as an inline SVG, drawn in
`--name`. It is the one glyph that carries the whole identity at 16px.

---

### 14. The day chart hides its own affordance until after you have found it

- Where: `server.py:902-903`. `.when::after` only says `(click to close)` when
  the `<details>` is already `[open]`.
- Breaks at: every width.

The sparkline is a 124x18 SVG inside a `<summary>` with `list-style:none`, so
there is no disclosure triangle and no hint. In the rendered page it reads as a
dotted rule with an orange smudge on the right. The one interactive element on
the list page is invisible as an interactive element until you have already
clicked it.

```css
details.chart summary .when::after { content: " (click for the day)"; }
details.chart[open] summary .when::after { content: " (click to close)"; }
```

Also give `summary` a focus ring. Right now `list-style:none` plus no outline
rule means a keyboard user tabbing the page gets whatever the browser default
is over a dark background, which on Chrome is a white halo and on Safari is
nothing.

---

### 15. The wordmark is identical between the board and the site; its colours are not

- Where: `server.py:990-997` (`LOGO_ROWS`) and `server.py:839-844` (the
  gradient), against `tools/mkscreens.py:176-193` and
  `tools/mkscreens.py:246-247`.
- This was checked by generating the board's wordmark from
  `wordmark_cells()` and comparing it row by row against the site's constant:
  **byte for byte identical**, six rows, 62 columns, including the micro sign's
  descender.

The colours are not.

| Row | Board (`make_welcome_ans`) | Site (`pre.logo i`) |
|---|---|---|
| 1 | `1;37` bright white | `#e2d4ff` lavender white |
| 2 | `1;36` bright cyan | `#b48ef0` purple |
| 3 | `0;36` cyan | `#8f7ae8` violet |
| 4 | `1;34` bright blue | `#6f84e0` indigo |
| 5 | `0;34` blue | `#4a7fc8` blue |
| 6 | `0;34` blue | `#3f6cab` blue |

Same letterforms, two different brands. A visitor who reads the site and then
dials the reference board in the next thirty seconds sees the wordmark twice in
two different colour families.

Pick one. My recommendation is to move the board's welcome screen to the site's
ramp rather than the other way round, because the site's `--name` purple is
already load-bearing across the listing (every board name is purple) and
because a six step RGB ramp is achievable on ANSI and PETSCII only as an
approximation, which is the direction that degrades gracefully. That is a
board-side change and belongs in the queue, not in this pass.

The rest of the palette already agrees and should not be touched: cyan means
structure on both, green means up on both, grey means quiet on both. The one
other disagreement is that the board uses LightRed for the sysop marker while
the site uses `--warm` amber for the sysop column, and amber on the board means
co-sysop. Low priority, but they are currently saying different things with the
same colour.

---

## What stays as it is

A consultant who finds everything wrong is not reading carefully. These were
checked and are right.

- **The 1080px cap on `main` and the centring** (`server.py:832`). At 1920 it
  leaves 411px of margin either side, which reads as deliberate rather than
  unfinished, and it is why finding 3 is a measure problem and not a container
  problem. Do not raise it.
- **The wordmark.** Identical to the board's, scales with `clamp(5px,
  calc((100vw - 44px) / 38), 15px)`, and was verified never to overflow:
  511px of ink in a 1080px column at desktop, 310px in a 358px column at 390.
  The comment claiming it never scrolls sideways is true. Only the gradient is
  in question.
- **The colour semantics.** Green is up and nothing else, cyan is structure,
  amber is the human, orange is activity. The same thing is the same colour on
  every page, which is the standard this project holds its own screens to, and
  the site meets it.
- **No JavaScript.** Everything in this report is achievable in CSS and Python.
  The one place the refusal costs something is the meta refresh (finding 10),
  and the answer there is to drop the refresh, not to add a script.
- **`.tablewrap`** (`server.py:942`). Markdown tables scroll inside their own
  box and never move the page. Measured at 390: the terminals table is 471px
  inside a 358px wrapper and the document scroll width stays 390. This is
  exactly the treatment the board list needs and does not have.
- **`.pull` at 73ch and `.freedom` at 71ch and `.warn` at 78ch.** These three
  were given measures on purpose and the measures are good. The body prose
  should join them, not the other way round.
- **`prefers-reduced-motion`** is handled in both places it needs to be
  (`server.py:867` for the nav blink, `server.py:1407` for the animated
  diagram), and the reduced version still carries the meaning. Nobody does this
  and it was done here.
- **The dial address as selectable text and a `telnet://` link at the same
  time**, with the host and port space separated so a selection pastes straight
  into `telnet host port`. Both audiences served with one string.
- **`tr:hover td { background:#111 }`.** Rows are not clickable, so hover
  highlight is arguably a false affordance, but across a 140 character row it
  is the thing that keeps your eye on one board. Keep it.
- **The empty state** (`server.py:1219`). "No boards listed yet. Yours could be
  the first." with 24px of padding. Most sites do not have one at all.

---

## What is missing rather than wrong

Not findings, because nothing is broken. Worth a decision.

- **The list has one sort order and does not say so.** `index_page` at
  `server.py:1194-1197` orders by online, then by caller-minutes, then by
  streak. A reader cannot tell that, and cannot change it. At eight boards it
  does not matter. At eighty it is the difference between a directory and a
  dump. A server side `?sort=busy|new|uptime` with three links in the header
  costs no JavaScript and no schema change.
- **There is no on-ramp above the table.** A visitor who has never called a
  board lands on a six column table of hostnames. The explanation of what to do
  is the second nav item and the explanation of what the figures mean is in the
  footer, below everything. The structural gap is that there is no slot between
  the lead and the table for one line aimed at a newcomer. Making the slot is
  my lane; filling it is the copy review's.
- **The header costs 346px before the first board on a phone**, measured: 55px
  wordmark, 80px two-row nav, 20px title, 63px lead, 24px of gaps. That is half
  the visible area of a 390x844 phone. The nav is the largest single item and
  it is seven links the visitor has not asked for yet. Worth considering
  whether the nav belongs above the list or below it on small screens; it is
  one `order` property on a flex `main`.

---

## Implementation order

Cheapest and most visible first. Everything in group A is CSS only and lands in
one edit to the style block.

**A. One CSS edit, no Python (about 40 lines).**

1. Finding 9, `.scene { overflow-x: auto; }`. One line, stops the about page
   scrolling sideways on every phone.
2. Finding 4, the type scale. Three rules. This is the change that makes every
   page look designed, and it is visible on all twelve pages instantly.
3. Finding 7, contrast. Two rules. Makes the footer readable.
4. Finding 3, the prose measure. Two rules.
5. Finding 2, the fixed column budget above 900px.
6. Finding 1, the card layout below 900px. This is the big one and it is still
   only the one block quoted above.
7. Finding 8, tap targets. Four rules.
8. Finding 11, table width scoping. Two rules.
9. Finding 14, the chart affordance. Two rules.
10. Finding 12, `aspect-ratio` on gallery images. One rule.

**Implementation note, and it will bite.** The stylesheet lives inside the
`PAGE` constant, which is consumed with `.format()`, so **every literal `{` and
`}` in the CSS must be doubled**. `@media` blocks and nested rules mean the new
code has more braces than anything already in there. A single un-doubled brace
raises `KeyError` at the first page render, not at import, so the server starts
and then 500s on the first request. Check with one `curl` to `/health` and one
to `/` before pushing to the repo.

**B. Small Python, one file each.**

11. Finding 5, per-page `<title>` from the Markdown's first `# ` line, plus an
    `<h1>` on the about page. About 6 lines.
12. Finding 6, pass `here` from the `/rules` and `/how` routes, drop the
    empty-string fallback, add a section map for the seven orphan pages. About
    12 lines.
13. Finding 2's supporting change: a `<br>` in the activity cell in
    `board_rows`, and `data-label` attributes on the six `<td>`s so the phone
    labels stop depending on column order. About 8 lines.
14. Finding 13, description and Open Graph tags, and a `µ` favicon.

**C. Decisions, not edits.**

15. Finding 10, the meta refresh. Rob's call: drop it and show a timestamp, or
    raise `DIRECTORY_LIST_REFRESH` to 300.
16. Finding 12's other half, resize `static/esp32.jpg` to 760px wide.
17. Finding 15, which wordmark gradient wins. Board-side change, belongs in the
    queue.
18. The three items under "what is missing", if they are wanted at all.

Groups A and B together are one deploy and they are the ones a visitor will
notice.
