# /install: choosing a board, and the three picks in gold

tty-ux, 2026-09-26, web, report only (saved by hand; the agent cannot write
files). Measured against the directory at 45e5b08 (site 1.3.11) on a
local server, from the stylesheet box model with Consolas metrics, no
browser, so figures are good to about 10 px. web-qa re-measures after the
build.

## Decisions taken with Rob (2026-09-26)

- The gold picks: **Cheapest** = ESP32 dev board (Base); **Most powerful**
  = Waveshare ESP32-S3-LCD-1.47; **Best with a camera** = **ESP32-CAM**
  (Rob chose it over the Freenove for the camera; its card-out install is
  stated plainly).
- Copy from internal/marketing-board-choice-2026-09-26.md (headline
  "First, pick your board"; "Not sure? Take an ESP32-S3 board if you
  can", SSH coming in 1.2.0), trimmed to the budgets below.

## The numbers

| | 1920 x 1080 | 1366 x 768 | 390 x 844 |
|---|---|---|---|
| root font | 21.28 px | 21.28 px | 18.40 px |
| one character | 10.24 px | 10.24 px | 8.86 px |
| main column | 1436 px, 140ch | 1323 px, 129ch | 354 px, 39ch |
| chrome above the article | 276 px | 276 px | about 380 px |
| card today | 26rem, 553 px | 26rem, 553 px | full width |

A 1366 x 768 laptop with a taskbar shows about 640 px of page.

## 1. The buttons are already under the fold; the picker in the card is why

The picker lives inside the install card, so every board adds 69 px above
the buttons. With four boards and the S3 chosen, Install runs from 726 to
781 and Update from 792 to 848 at 1366. With the ESP32 chosen, Update
ends 8 px past 768. The 1.3.5 CHANGELOG claim that Update still starts on
the first screen is true only for the ESP32.

Fix:
- The picker (`fieldset.boards`) leaves the card and becomes the last
  thing in the intro column from 901 px up: h1, lead, then the picker, as
  step 1. The card holds only step 2.
- The card narrows from 26rem to 24rem and gains a top line per board:
  the board's picture at 3rem x 1.875rem and "For the <name>", so the
  picture check stays in view beside the buttons.
- The generated per-board CSS rules key on `.install-top:has(#fwbN:checked)`
  instead of `.installer:has(...)`.
- Result: worst case (the S3) Install runs 503-559 and Update 569-625,
  whatever the number of boards.
- Phone markup order: title, lead, picker, card, steps.

## 2. No board is chosen for the reader

`installer_html()` pre-checks the first board, so its buttons are live
before anyone looks at a picture, and three boards share one chip family.

- No radio is `checked`. Until a pick, the card shows step 2 with "Pick
  your board first. Its buttons appear here." (--dim, 0.8125rem, two
  lines) and no buttons.
- Every `.bsec` is hidden by default and shown by
  `.install-top:has(#fwbN:checked) .bsec.bN { display:flex }`;
  `.install-top:has(input[name=fwboard]:checked) .pickfirst { display:none }`.
- Fallback: `@supports not selector(:has(a)) { .bsec.b0 { display:flex } .pickfirst { display:none } }`.
- The keyboard needs no change; a reload keeps the pick.

## 3. The guide sits at the head of step 1, in the left column

Not above both columns (that pushes the card about 110 px) and not in the
card.

- The legend becomes a step heading, `1  Pick your board`: --struct,
  0.9375rem, uppercase, "which is mine?" on the same line at the right,
  still a real `<legend>`. The card's first line is `2  Install it`.
- One direction line, `p.how` (--ink, 0.8125rem, at most 80 characters),
  e.g. "Have one? Choose the row that looks like it. Buying one? Start here:".
- `ul.uc`: one item per pick, in row order: the label in gold, the
  board's short name, and why. Desktop: a grid
  `max-content max-content minmax(0,1fr)`, column-gap 2ch, 0.8125rem,
  line-height 1.5 (78 px for three). Below 1200 px each item is a block,
  the label on its own line and the name and why indented 2ch.
- Each board name in the guide is a `<label for="fwbN">`: plain HTML
  selects the row, with no script. Styled --dial, dotted underline,
  cursor pointer. `.install-top:has(#fwbN:checked) .uc label[for=fwbN]`
  fills it #102630. Optional: hovering a guide name outlines its row with
  a 1px dashed --dial outline. A label is not focusable, so the guide may
  never say anything the rows don't.
- No row of chips.

## 4. The gold frame

- `border:1px solid #8a6d39` on `.bopt.rec` (the amber box's existing bar
  colour, 4.05:1 against the page). Unframed rows keep #2c3a44. Not
  --warm itself, which would out-shout the Install button.
- Selection for every row becomes a #102630 fill plus
  `box-shadow: inset 3px 0 0 var(--dial)`, and the border is left alone,
  so gold always means "our pick" and cyan "your choice".
- Focus is unchanged (3px #ffd35c outline, offset 2px).
- No seals on /install. On /hardware, don't frame the pictures (the
  ribbons would cut a frame): add a first facts row
  `<dt>Our pick</dt><dd>Best with a camera</dd>` instead.
- Forced colours: `.bopt.rec` gets a 2px border and the tag
  `border:1px solid CanvasText; padding:0 0.25em`.

## 5. The tag on a framed row

- On the name line, right side, top-aligned:
  `<span class="nl"><b>name</b><span class="rec">...</span></span>`,
  with `display:flex; flex-wrap:wrap; justify-content:space-between;
  align-items:baseline; column-gap:1ch`. It wraps under the name where it
  doesn't fit (below about 1200 px and on a phone).
- 0.6875rem, uppercase, letter-spacing 0.04em, --warm, no box.
- At most 18 characters.
- One source: a `"rec"` field on each picked `BOARDS` entry holding the
  label, the order, a short name (at most 16 characters) and a why (at
  most 34 characters). The guide, the tag and /hardware all read it.
- Rows become a grid:
  `.bopt { display:grid; grid-template-columns: auto 4rem minmax(0,1fr);
  column-gap:0.625rem; align-items:center }` with no flex-wrap. That fixes
  the text dropping under the picture on a phone.
- The row's version line shows only the core version and "preview"
  ("Firmware 1.1.1 preview"); the full string stays under the buttons.

## 6. Row order

- The picks first, in a fixed order that matches the guide: Cheapest,
  Most powerful, Best with a camera.
- Then the other boards in `BOARDS` order, under an "Also runs on"
  heading (--dim, 0.8125rem). With three of four framed, the heading is
  what makes the frames read as recommendations.
- No default selection.

## 7. Accessibility

- The tag is prefixed `<span class="vh">Our pick: </span>`.
- Each radio gets `aria-labelledby` (the name and tag spans) and
  `aria-describedby` (the pick line and the version line).
- The fieldset gets `aria-describedby` pointing at `p.how`.
- The frame is decorative: the tag, the heading and forced colours carry
  the meaning.
- The placeholder is plain text in the card, not aria-live.

## 8. On a phone

The phone is where people choose and buy, not install, so the guide
matters more there. Rows run full width (354 px); the picture stays 4rem x
2.5rem; the tag wraps under the name (+17 px a framed row); the card
follows the list.

## What stays

Native radios and `:has()` with no script; the row content; the two
full-width buttons and their icons; the amber "Before you start" box and
the same-chip line (below the buttons); the sticky card; no seals on the
picker; --dial for selection and anything pressable.

## Implementation order (each ships on its own)

1. No default pick: drop `checked`, add the placeholder, hide the
   sections until `:has()`, add the `@supports` fallback.
2. The picker to the left column; the card to 24rem with its "For the
   <name>" line.
3. The row grid and the shorter version line.
4. Selection as a fill plus a left bar.
5. `rec` in `BOARDS`, then the order, frame, tag and "Also runs on", with
   the .vh prefix and the explicit radio names.
6. The guide list with `<label for>` names, and the "Our pick" row on
   /hardware.
7. web-qa at 1920 x 1080, 1366 x 768, 390 x 844 and 1366 x 640 with the S3
   chosen. Pass: Update's bottom edge at or above 640 for every board at
   1366, and all three picks plus the guide on the first screen at 1920.
   Then correct the 1.3.5 CHANGELOG claim and the `installer_html()`
   comment.

Copy budgets for the marketing text: the legend and `p.how` within 80
characters on one line; three labels at most 18 characters; three short
names at most 16; three whys at most 34; the placeholder in two lines of
44; and step 1 of "What happens, in order" reworded, since the board is
now chosen in step 1 above.
