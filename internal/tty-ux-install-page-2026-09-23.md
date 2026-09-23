# /install: layout spec, 2026-09-23

Rendered from the local server (127.0.0.1:8791, firmware/0.23.0 present) at
1920, 1366 and 390. Desktop rem 21.28px, body 18.62px; phone rem 18.4px.

## Summary

The button is 550px down under two paragraphs, the steps start at 1615px
(1.5 screens at 1080), and the card states the version three times. At
390 the button is at y=880, under the 844 fold. Fix: a one line lead, then
a two column grid at the site's existing 901px breakpoint: steps and
drawings left, a 22rem sticky card right. Phone: one column, button first.

## Disagreements for Rob

- Phone only: the amber box goes after the button. The nav takes y 100 to
  330 at 390; a box ahead puts the button near y 720, on screen at 844 and
  off it at 667. Desktop keeps it ahead.
- Grid column, not a float. Same top right look; sticky fails on a float.

## Measured now

- Article column: 1436px at 1920, 1323 at 1366, 353 at 390.
- Card: full width, padding 1.125rem 1.25rem (24 x 27px), 276px tall.
  Button 340 x 61px. Four `.meta` lines at 0.8125rem, 28px each.
- Step drawings: viewBox 354 wide, `max-width:34rem` (723px), centered
  with 355px of black each side at 1920. Heights 243, 231, 223, 243, 268.
  At 390 they draw 355 x 120, 1:1.
- Before you start: 844 x 208px blockquote plus a 295px list, 776px of
  page before the steps begin.

## 1. Desktop, 901px and up

```
242                        1210  1252            1678
|------ left: steps -------| gap |-- card 22rem --|
PUT THE BBS ON YOUR BOARD
one line lead
What happens, in order        +----------------+
[drawing 723px, left]         | [mini art]     |
1. Press the button ...       | |amber 3 lines |
2. The page reads ...         | Board: ESP32   |
[drawing]                     | [ Install ...] |
5. ...                        | 0.23.0, date   |
[drawing]                     | what is inside |
7. The address ...            +----------------+ sticky
|----- full width from here: Before you start ... -----|
```

- `.install-top`: `display:grid; grid-template-columns:minmax(0,1fr) 22rem;
  column-gap:2rem; align-items:start`. Left column 925px at 1920, 812px
  at 1366; both hold the 723px drawings, now `margin:1rem 0 1.25rem`,
  left aligned.
- Card: `position:sticky; top:1rem; width:22rem` (468px), padding and
  border as now, inner 19.5rem (415px), `display:flex;
  flex-direction:column; gap:0.75rem`.
- Contents in order:
  - mini drawing, 415 x 113px (section 5)
  - amber box `.installer .pre`, styled as `.installer .no` (#f0c674 on
    #241d10, 3px #8a6d39 left border, padding 0.625rem 0.875rem,
    0.8125rem): three one line items and a link line, five lines at most
  - picker slot, 1.5rem tall (section 6)
  - the button, `width:100%` (415px), 61px tall
  - one `.meta` line: version, date, chip, from release.txt only; the
    other two version lines go
  - one `.meta` line: the notices link, its text not a bare number
  - the ESP Web Tools licence line moves to "Doing it the other way"
- Card about 490px tall, inside the 740px available when stuck at 768.
  Button at about y 690 on load at 1366 x 768, on screen.

## 2. The amber box: anchor, not `<details>`

The link goes to `#before-you-start`, the first h2 under the grid. An
opened `<details>` adds a 776px section to a card with 740px in total at
768 tall, and a sticky card taller than the viewport hides its own
button. An anchor is one line, no JavaScript, one screen away. `md_render`
(server.py:1127) emits `<h2>` with no id: add one slugged from the heading.

## 3. Phone, 900px and under

- One column: h1, lead, card, steps, the rest. Nothing sticky.
- Card by flex `order`: picker slot, button, version, notices, amber box,
  mini drawing last.
- At 390: card top about y 520, button 577 to 630, on the first screen at
  844 and at 667. On iOS the amber "cannot" message takes the button's
  place.
- Mini drawing at 307px is 0.87 scale, label 7.8px: fine for an
  `aria-hidden` decoration at the bottom.

## 4. Sections under the grid

Before you start (full, with the anchor), What it does to the chip, Wi-Fi,
Changing the Wi-Fi later, Reset rather than reflash, When the board does
not appear, After it boots, The sysop password, Doing it the other way
(moved last: it is the exit), Then. Collapse none: Firefox and Safari do
not search inside a closed `<details>`, and troubleshooting is what a
stuck reader searches for.

## 5. The mini drawing

`svg.art.mini`, viewBox `0 0 354 96`, `width:100%; max-width:none;
height:auto; background:none; border:0; margin:0`. No box in a box. Same
hand as INSTALL_CABLE, caption dropped:

- laptop: `.o` outline 100 x 64 at (8,10), `.g` screen, `.k` pill with
  "Install" at 9 units, base plate to y 84
- cable x 116 to 200: `.o` sleeve over a `.lt` conductor
- `_board(206, 26)` as is, 120 x 52, lamp `.lf`

At 415px the scale is 1.17, label 10.5px.

## 6. The board or version picker

- Today: hidden. The slot holds a static `.meta` line, "Board: ESP32,
  4 MB flash", 1.5rem tall, so nothing moves when a picker arrives. A
  radio group of one is a control that does nothing, the argument
  `installer_html` already makes about a button with no release.
- Two or more: native radios, no JavaScript, `display:flex; flex-wrap:wrap;
  gap:0.25rem 1rem`, labels 0.8125rem, one `esp-web-install-button` per
  manifest shown by `input:checked ~` sibling CSS. Not a `<select>`: it
  cannot switch a manifest without a script.
- ESP32 and ESP32-S3 share one manifest; ESP Web Tools picks by chip. A
  row exists only for boards sharing a chip (generic S3 against the
  Waveshare LCD) and, as a second row, for the kept older release, which
  replaces today's "Install x instead" button.

## Implementation order

- Card contents: one version line, licence out, button full width.
- Grid, sticky, h2 ids, the anchor.
- Mini drawing, then the phone `order` rules.
