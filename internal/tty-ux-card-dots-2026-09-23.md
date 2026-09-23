# tty-ux: the Run-your-own card's lamps, 2026-09-23

Site 0.21.1, `.runcard` in `server.py` ~2591-2617, rendered on 127.0.0.1:8792 at 1366x900.

## Verdict

Fixable in CSS alone. Three lamps a third of a lap apart look scattered because a
413x160 px rectangle has no three-fold symmetry. Two lamps half a lap apart are
always point-symmetric through the centre (the point at s + P/2 is the reflection
of the point at s), so on any card shape they read as a pair. Give each a short
bead tail on the border line and it is a slow patrol, not beads on a wire.

```
          o=-.------------------------------------,
          |  Run your own board                   |
          |  An ESP32, a USB cable, five minutes. |
          |  [Web installer] [Build from source]  |
          `------------------------------------.-=o
```
A just past the top-left corner heading right, B just past the bottom-right
heading left. Same relation at every instant.

## Spec

- Two lamps, each one head span plus three tail spans, all `aria-hidden`.
- Head: 0.375rem, `var(--dial)`, `box-shadow:0 0 0.375rem rgba(127,212,255,0.8)`.
  Identical to the hover lamp under a board name: same object, different tempo.
  Hover is 1 s once on demand; this is 20 s, ambient.
- Path: keep `offset-path:inset(0 round 0.5rem)`, on the border line. Inside it
  competes with the buttons; on the frame it is the frame.
- Duration 20 s, `linear` (easing lurches at a loop's seam). 56 px/s at 1366.
  16 s felt busy because three things moved, not because it was fast.
- Tail: three followers on the same path, not a gradient bar. A 1.75rem bar with
  `offset-rotate:auto` pokes 26 px past each 0.5rem corner; beads bend.
  - Sizes 0.25 / 0.1875 / 0.125rem, opacity 0.7 / 0.45 / 0.2,
    glow `0 0 0.25rem rgba(127,212,255,0.6)`. Tail reaches 1rem behind the head.
  - Delays, lamp A: head -0.36s, followers -0.24s, -0.12s, 0s.
    Lamp B: head -10.36s, followers -10.24s, -10.12s, -10s. All negative, so no
    jump at load. Static `offset-distance`: A 0%, B 50%.
- Followers are `opacity:0` at base, given opacity only inside the no-preference
  plus `@supports` block.

## Reduced motion and fallback

Two still lamps at 0% and 50%, just past the top-left and bottom-right corners,
no tails: corner marks, like the bracketed panel top right. Without
`offset-path`: A `top:-0.25rem; left:0.5rem`, B `bottom:-0.25rem; right:0.5rem`.
