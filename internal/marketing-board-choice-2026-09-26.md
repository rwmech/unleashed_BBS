# Choosing a board: the guide at the top of /install (marketing, 2026-09-26)

Rob: "Build yours" lands on /install, and the top of that page needs a clear
"how to choose a board" guide. People pick a board first, by what they want
it for. Recommend the S3 boards, since SSH is coming to them. Three boards
get a thin gold frame as our picks.

This is a proposal. Nothing on the site has changed. Rob picks; the approved
lines go to `explain` to place.

Facts checked against the directory repo's `server.py` (BOARDS, SOON_BOARDS,
NOT_BOARDS) and `pages/hardware.md` as of site 1.3.11, and the firmware's
`tools/release.py` at v1.1.1 (which builds all four board images, so both
camera boards are on a full release). Voice follows round 3 of
`internal/marketing-2026-09-25.md`: plain words first, the hobby's words
second.

## The three gold picks, today

| Frame | Board | Why, in one line |
|---|---|---|
| Lowest cost | **ESP32 dev board (Base)**, about $5 | A whole community board for the price of a coffee: chat, messages and accounts, installed in five minutes, and a card added later for forums and files. |
| Most capable, no camera | **Waveshare ESP32-S3-LCD-1.47**, about $20 | The strongest board we have tested: 16 MB of flash, 8 MB of PSRAM, a card slot and a small colour screen built in, no wiring, and first in line for encrypted connections (SSH) in firmware 1.2.0. |
| With a camera | **Freenove ESP32 camera board**, about $19 | The easiest camera board: the camera, a card slot and a card in the box, a USB-C socket, a BOOT button, and 8 MB of PSRAM. |

On the camera pick, because it is the close call:

- The Freenove wins on the whole board: nothing to take out before
  installing, a BOOT button (so the backup window and the BOOT-hold recovery
  both work), USB-C, spare pins, and the card in the box.
- Its weakness is the camera itself. It varies by batch: an OV2640, or a
  GC0308 limited to 640x480 as Rob's came. The site already says so; the
  pick's copy must say it too, not hide it behind the gold.
- The ESP32-CAM has the better camera for sure (a genuine OV2640, 2 MP, up
  to 1600x1200) at about $9, but it sits on a programmer board, its card
  has to come out to install, and it has no BOOT button. That is a fine
  board for someone who wants the sharpest photo for the money, and a poor
  first recommendation. It gets a use case below, not the gold.
- If Rob would rather the gold follow the photo quality, swap it to the
  ESP32-CAM and relabel it "Sharpest photos". I would not: the frame reads
  as "start here", and the ESP32-CAM is not a start-here board.

### When the new boards land

- **The ESP32-S3 camera board should take the camera gold** the day it has
  been tested and a release carries its image: an S3 (so SSH), 16 MB of
  flash, 8 MB of PSRAM and a 3 MP OV3660, against the Freenove's classic
  ESP32 and its camera lottery. It also becomes the one board that matches
  the "recommend the S3" advice and has a camera. Not before: it is untested,
  and boards sold under that name differ (the site says so). No gold frame
  on a "coming soon" board, ever.
- **The Makerfabs 3.5" board should not take the "most capable" gold.** Its
  screen is the big draw (480x320, touch), but it carries 2 MB of PSRAM
  against the Waveshare's 8 MB (Makerfabs lists the ESP32-S3-WROOM-1-N16R2
  module), and a screen that size wants its PSRAM for drawing, so fewer
  encrypted connections at once. The Waveshare stays the most capable. The
  Makerfabs gets its own use case ("a big status screen") once it is on the
  site. Note: it is in no table on the site yet (not in SOON_BOARDS), and
  the retro machine skins exist only as a plan, so its card stays off the
  guide until it is at least listed as coming. Confirm which Makerfabs model
  is meant (the SPI or the parallel version) before anything is written
  about it.
- **The lowest cost pick does not change.** Nothing on the list undercuts
  the $5 dev board.

## Headline and intro

Recommended:

> ## First, pick your board
>
> Every board here runs the same software and installs from this page. What
> changes is what you can add: a screen, a camera, room to grow. Choose by
> what you want your community to do, then install.
>
> Not sure? Take an ESP32-S3 board if you can. They are the ones getting
> encrypted connections, coming in firmware 1.2.0.

Alternates for the headline, same intro:

- **Start by choosing your board**
- **Which board is yours?**

Why the recommended one: "First" is the instruction Rob asked for (pick a
board before anything else), and it is two words shorter than the others.
The intro answers the fear a newcomer has at this point, "will I buy the
wrong one?", with "they all run the same software", then gives the one
tie-breaker we want them to use.

## Use cases

Each maps to one board. Headline, then one line of copy. Items marked
coming stay out of the guide until their board is on the site.

- **Start small, spend least.** ESP32 dev board (Base), about $5. Chat,
  messages and accounts for up to ten people at once, and you can add a
  card later.
- **A club with forums and a file library, on a budget.** ESP32 dev board
  (Base) with an SD card module, about $8. A little wiring, about half an
  hour, and a shared library of files people can download.
- **The most capable, ready for encrypted connections.** Waveshare
  ESP32-S3-LCD-1.47, about $20. The most memory we have tested, a card slot
  and a small screen showing who is on, with SSH coming in firmware 1.2.0.
- **A board with a camera anyone can use.** Freenove ESP32 camera board,
  about $19. Someone joins, types a command, and downloads a photo of
  whatever the board is looking at: a bird feeder, a workbench, the view.
  The card comes in the box.
- **The sharpest photos for the least money.** ESP32-CAM, about $9. A 2 MP
  camera, photos up to 1600x1200. Take the card out while you install, then
  put it back.
- **A big status screen on your desk.** (Coming) The Makerfabs ESP32-S3
  board with a 3.5" touch screen, showing your board and who is on. Until
  it arrives, the Waveshare's small screen does the job.

One the list could add later, and should not now: "a camera and encrypted
connections" (the S3 camera board), once it has run.

## Gold frame labels

Recommended, short enough for a thin frame at 390 px:

- **Our pick: lowest cost** (the dev board)
- **Our pick: most capable** (the Waveshare)
- **Our pick: with a camera** (the Freenove)

Why "lowest cost" and not "best value": best value is a judgement a reader
can argue with (the Waveshare is arguably better value at four times the
price), while lowest cost is a fact. If Rob prefers the warmer word, "Our
pick: best value" is fine on the dev board, since $5 for a whole board is
hard to argue with.

If the frame is too narrow for "Our pick:", drop it and let the gold carry
it: **Lowest cost**, **Most capable**, **With a camera**.

## Notes for explain

- The gold frame should sit on the board's card in the installer's picker
  and on its block on /hardware, so the pick is the same in both places.
- Say the camera varies on the Freenove's pick, in the pick's own line.
- "Recommend the S3" is a sentence in the intro, not a frame: two of the
  three picks are classic ESP32 boards today, and the intro should not
  read as though it contradicts the frames. When the S3 camera board takes
  the camera gold, two of three picks are S3 and the sentence gets
  stronger by itself.
- No machine names in the guide; nothing in it is about who joins, only
  about what the board can do.

Source for the Makerfabs module: [Makerfabs, ESP32-S3 SPI TFT with Touch
3.5" ILI9488](https://www.makerfabs.com/esp32-s3-spi-tft-with-touch-ili9488.html)
(ESP32-S3-WROOM-1-N16R2, 16 MB flash, 2 MB PSRAM, 480x320, micro SD slot).
