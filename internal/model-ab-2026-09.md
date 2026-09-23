# Fable against Opus: code-review and optimize, ten runs each

Rob, 2026-09-22: "Can we run a test for the next 10 code reviews / optimize
to run through fable and then opus and compare with actual numbers vs
feelings."

The point of writing the scoring down first is that a model comparison judged
afterwards is judged on which report read better, and the better-reading
report is not reliably the more correct one. This project has already been
burned by exactly that: an invented mechanism written into a code comment as
fact was persuasive *because* the arithmetic around it was right.

## Settings

| Agent | Was | Now |
|---|---|---|
| `code-review` | opus | **fable** |
| `optimize` | opus | **fable** |
| `screen-artist` | opus | **fable** |
| `tty-ux` | opus | **fable** |
| `explain` | opus | **opus**, Rob's call: it is the copy readers judge the project by |
| `bbs-qa`, `web-qa`, `docs`, `bbs-regression`, `web-regression` | sonnet | **sonnet** |

**Sonnet is the floor** (Rob: "I would use sonnet at the minimum. Don't go
down to Hiku"). I had suggested haiku for the regression runners and docs;
overruled, and the reasoning holds up: this project's expensive failures have
all been confident wrong answers rather than slow ones, and a cheaper model
reviewing a screen will tell you it looks fine.

## Protocol

For each of the next ten reviews and ten optimize runs: **run fable first,
then opus on the same diff or the same tree**, without showing either one the
other's output. Record both before reconciling.

Running fable first is deliberate. If opus goes first its findings are in the
conversation, and the second run is then scored on agreement rather than on
what it found by itself.

## What gets counted

Only things that can be checked. No "read better", no "more thorough".

### code-review

| Metric | How it is measured |
|---|---|
| **Confirmed findings** | The finding names a real defect, verified by reading the code or reproducing it. The unit of merit |
| **False positives** | Reported as a defect, and the code is correct. The expensive failure mode: each one costs a verification round |
| **Missed** | Found by the other model, or by Rob on the board afterwards, and not by this one |
| **Unique confirmed** | Confirmed findings the other model did not report. What the second model is actually worth |
| **Severity** | Of the confirmed ones: live bug / latent bug / style. Only the first two count toward the total |
| **Wall clock, tokens** | From the task notification |

### optimize

| Metric | How it is measured |
|---|---|
| **Byte figures correct** | Re-measured off the ELF with `nm`. A figure that does not reproduce is a false positive, however plausible |
| **Recommendations that survive reading the code** | The 0.21.3 `exp_`/`imp_` union was approved off a report and then withdrawn on reading the code, because both types own open `FILE*` handles. A recommendation that does not survive that is not a recommendation |
| **Breakage predicted** | Did it say what would break, or only what would be saved? Every buffer here is the size it is because something failed at a smaller size |
| **Missed** | Large static objects it did not rank |

## Scoring

```
value = confirmed_real_bugs - (2 x false_positives)
```

False positives count double because a wrong finding costs a verification
round and, worse, trains the reader to skim the next report. That weighting
is the argument, so it is stated up front rather than chosen once the numbers
are in.

Cost is recorded but is **not** in the score. A cheaper model that misses a
live bug is not cheaper: the `CONFIG files` permission bug shipped and was
live, and the announce out-of-bounds read went three versions.

## Results

| # | Agent | Task | Model | Confirmed | False + | Unique | Value | Tokens | Wall |
|---|---|---|---|---|---|---|---|---|---|
| | | | | | | | | | |

## Running notes

_One line per pair, naming anything the table cannot hold: a finding one
model described correctly and the other described as the wrong mechanism, a
byte figure that did not reproduce, a recommendation that did not survive
contact with the source._

## Reading it afterwards

Ten is enough to see a difference in false-positive rate and not enough to
settle a small difference in confirmed findings. If the totals land within
one or two of each other, the honest conclusion is "no measured difference at
this sample size, use the cheaper one", not a winner.
