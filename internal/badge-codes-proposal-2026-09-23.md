# Badge codes: a proposal for the directory (2026-09-23)

Working note from the website agent, for the site version that ships with
firmware 1.0.2. Nothing here is built. Site 1.0.0 keeps the 0.22.2 slugs;
Rob moved the short codes out of it (2026-09-23).

## What Rob asked for

- Shorter slugs: "like 5 or 6 max, I cant imagine we need more than that to
  really understand the code". A sysop types them into a 95 character
  CONFIG field, up to 16 per list.
- Short codes in the style of his example, `MNTLH` for Mental health: five
  characters where possible, six at most. A real word or number stays whole
  when it fits (HAM, C64, DOS, ZX, LINUX, ANIME, BOOKS); otherwise a
  consonant-style abbreviation (MNTLH, NEURO, VETS, BRST, DMNTA, RCVRY,
  DONOR, HMLSS).
- Shown UPPER CASE as codes on /badges and in the filter search
  ("MNTLH  Mental health"). The protocol and the stored values stay lower
  case, because the firmware lower-cases on send, so input is
  case-insensitive everywhere: announce, `?b=` and the search box.
- Every old long slug, and every interim slug proposed along the way, stays
  accepted as an alias.

## The rule used below

- A real word, acronym or number of six characters or fewer that names the
  thing stays whole: LGBTQ, CANCER, HUNGER, C64, APPLE2, ARCADE.
- Anything longer becomes a consonant code of five or fewer, in the MNTLH
  pattern: the first word's consonants, then the second word's initial when
  there is one (MNTL + H, CHLD + C, BRDG + M).
- Unique across both lists and against the other filter keys (petscii,
  guests, chat, forums, files, mail, doors, new, steady, update, 1m, 6m, 1y,
  2y, 5y, 10y).

## Support, 24

| Code | Name on /badges | Slug today (0.22.2) |
|---|---|---|
| LGBTQ | LGBTQ+ people | lgbtq |
| TRANS | Transgender people | trans |
| DSBLD | Disabled people | disability |
| NEURO | Neurodiversity | neurodiversity |
| MNTLH | Mental health | mental-health |
| SCDPV | Suicide prevention | suicide-prevention |
| VETS | Veterans | veterans |
| CANCER | People with cancer | cancer |
| HIV | People living with HIV | hiv |
| ANIMAL | Animal welfare | animals |
| BRST | Breast cancer awareness | breast-cancer |
| CHLDC | Children with cancer | childhood-cancer |
| DMNTA | People with dementia | dementia |
| CARERS | Carers and caregivers | caregivers |
| DBTS | People with diabetes | diabetes |
| HEART | Heart health | heart-health |
| DV | Survivors of domestic violence | domestic-violence |
| RCVRY | Addiction recovery | recovery |
| DONOR | Blood and organ donation | donation |
| FOSTER | Foster care and adoption | foster-adoption |
| HMLSS | People without a home | homelessness |
| HUNGER | Hunger relief | hunger |
| LTRCY | Literacy | literacy |
| FRSTR | First responders | first-responders |

## Interests, 43

| Code | Name on /badges | Slug today (0.22.2) |
|---|---|---|
| BBS | BBS history | bbs-history |
| LINUX | Linux | linux |
| OSS | Open source | open-source |
| PRGRM | Programming | programming |
| RETRO | Retrocomputing | retrocomputing |
| AMIGA | Amiga | amiga |
| APPLE2 | Apple II | apple2 |
| ATARI | Atari | atari |
| C64 | Commodore 64 | c64 |
| DOS | DOS | dos |
| ZX | ZX Spectrum | spectrum |
| 3DPRT | 3D printing | 3d-printing |
| ELCTR | Electronics | electronics |
| ROBOT | Robotics | robotics |
| SOLDER | Soldering | soldering |
| WOOD | Woodworking | woodworking |
| ARCADE | Arcade and pinball | arcade |
| BRDGM | Board games | board-games |
| GAMES | Gaming | gaming |
| RTRGM | Retro gaming | retro-gaming |
| RPG | Tabletop RPGs | tabletop-rpg |
| ANSI | ANSI art | ansi-art |
| CHPTN | Chiptune | chiptune |
| DEMO | Demoscene | demoscene |
| DRAW | Drawing | drawing |
| MUSIC | Music | music |
| PHOTO | Photography | photography |
| HAM | Amateur radio | ham |
| ASTRO | Astronomy | astronomy |
| SWL | Shortwave listening | swl |
| WTHR | Weather | weather |
| AVTN | Aviation | aviation |
| CARS | Cars | cars |
| COOK | Cooking | cooking |
| BIKE | Cycling | cycling |
| FISH | Fishing | fishing |
| GARDEN | Gardening | gardening |
| HIKE | Hiking | hiking |
| TRAINS | Model trains | model-trains |
| ANIME | Anime | anime |
| BOOKS | Books | books |
| MOVIES | Movies | movies |
| SCIFI | Science fiction | scifi |

## Choices worth a second look

- **SCDPV, suicide prevention.** The code keeps the word "suicide" out of a
  sysop's config line, where "support = suicide" would read as the opposite
  of what is meant. HOPE is the alternative if a real word is wanted; "LIFE"
  was rejected because it reads as a side in an argument the support list
  stays out of.
- **DV** rather than DMSTV: DV is the abbreviation the sector itself uses.
- **ANSI, ANSI art.** `ansi` is also a value of the `terminals` field. The
  two never meet: terminals contribute only `petscii` to the filter's key
  space. If ANSI terminals ever become a filter key, that key needs a
  different name.
- **3DPRT** rather than 3D, because "3d" in a filter link reads like the
  time-listed keys (1m, 1y).
- **BRDGM** rather than BOARD: every listing on the page is a board.
- **ROBOT, SOLDER, GARDEN, TRAINS** keep the real word, which fits in six,
  rather than a consonant code.

## Aliases to accept

Every slug in the "Slug today" columns above, which is what boards and
bookmarks carry now. Also each old slug with its hyphens removed, because
the firmware keeps a-z, 0-9 and "-" from what a sysop types, so "Mental
health" arrives as `mentalhealth`: bbshistory, opensource, 3dprinting,
boardgames, retrogaming, tabletoprpg, ansiart, modeltrains, mentalhealth,
suicideprevention, breastcancer, childhoodcancer, hearthealth,
domesticviolence, fosteradoption, firstresponders.

And the interim slugs proposed on the way here, none of which ever shipped:

- support: access, neuro, mental, lifeline, hope, vets, animal, bcancer,
  breast, ccancer, kidca, memory, carers, diabet, heart, dv, sober, donor,
  foster, homeless, homes, read, 1stresp, rescue;
- interests: bbs, oss, code, retro, zx, 3dprint, 3d, elec, robots, solder,
  wood, boardgm, meeple, games, retrogm, retrog, rpg, ansiart, ansi, chip,
  demo, draw, photo, astro, wx, fly, cook, bike, fish, garden, hike, trains.

Where an interim slug is the same as its new code (neuro, vets, heart, dv,
donor, foster, carers, bbs, oss, retro, zx, games, rpg, ansi, demo, draw,
photo, astro, fish, hike, trains) it needs no alias entry.

## What the build needs, from the attempt that was backed out

- `SLUG_ALIASES` in server.py, old to new, read in three places: `pick()` on
  an announce, `row_support()` / `row_interests()` on a stored row (so no
  migration), and `filter_query()` on `?b=`. The attempt did all three and
  it worked; it is in the site repo's history only as this note.
- `INTEREST_ART` is keyed by slug, so every interest drawing's key changes
  with it.
- `badge_words()` should include the old slugs, so a search for
  "electronics" still finds ELCTR.
- /badges shows the code upper case; the filter search matches either
  case; `data-k` stays lower case.
- PROTOCOL.md: the codes are canonical, lower case on the wire, the old
  slugs are aliases.
- selftest: every code six characters or fewer, unique, the /badges display
  upper case, and every old slug mapping to its code on announce and in
  `?b=`.
- **The firmware's own badge test will need its expectations changed**:
  `tools/testclient.py` (the directory badges block, about line 5520)
  asserts the directory's JSON returns `["lgbtq", "literacy"]` and
  `["c64", "electronics", "ham"]`. Sending those still works through the
  aliases, but the JSON will answer `ltrcy` and `elctr`. ANNOUNCE.md,
  `data/system.cfg.example` and the comment in `announce.cpp` use the same
  examples.
