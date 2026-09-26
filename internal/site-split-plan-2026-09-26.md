# Site split, 2026-09-26: plan, what was built, and the droplet steps

## Summary (10 lines)

- **unleashed_directory 2.0.0** (public, GPL-3.0-or-later, unleashedbbs.net): list at `/`, announce, badges, data, how, rules; renders the guides at `/docs` and the stock skins at `/skins`; canonical `sitekit.py`. Branch `dirsplit` on the worktree, **not pushed**.
- **unleashed_documentation** (public, CC BY-SA 4.0): 15 guides + index, the CONFIG captures, stock skins, `check.py`. Pushed, `cdc0c05`.
- **unleashed_site 1.4.0** (private, all rights reserved, .com + .org): pitch, installer + fetcher, hardware, satellites, marketing, manifesto, author; /directory drawn from the directory's JSON with a kept copy. Pushed, `22adc7f`.
- Shared code: `sitekit.py` edited in the directory, copied byte-for-byte into the site; the site's suite fails when the copies differ.
- Every old URL keeps working: moved pages answer with a 301 in the app that no longer owns them; `/announce` is proxied on all three domains (the firmware treats any 3xx as "refused").
- The installer places each image set by its own `partitions.bin`; an S3 on anything before 1.1.2 is always erased (dialog change 6, `EWT_REV` 5).
- Tests: baseline 969 checks. After: directory 524, site 636, docs 14; all pass. Every baseline label runs in one of the new suites (12 were renamed, e.g. `/forward renders` became `/docs/forward renders`).
- Deploy: two services (8080 directory, 8081 site), one Caddy, one file per service in `/etc/caddy/sites/`. The directory's `setup.sh` switches Caddy before restarting, so no page is answered by the wrong server.
- Nothing is deployed. The directory push goes live through autopublish, so it waits for Rob's go and the steps below.
- Not visually checked: the guard refuses the Chrome path from this session, so no page was rendered in a browser.

## 1. Page inventory (as built)

| Old path | Now | Why |
|---|---|---|
| `/` on .com (pitch) | site, .com `/` | sales |
| `/` on .org (manifesto), `/about`, `/author` | site, .org `/`, `/author` (`/about` on .com 301s to .org) | the argument |
| `/` on .net (data face) | directory: `/` is the board list, data at `/data` | .net is the directory |
| `/directory` | site: drawn from JSON; directory: the full list with filter and search | Rob: .com shows the live list |
| `/directory?b=`/`?q=`, `/?b=` on .com | 301 to `https://unleashedbbs.net/?...` | only the directory can filter |
| `/badges`, `/how`, `/rules`, `/data`, `/feed.xml`, `/api/boards.json` | directory; 301 from .com | directory pages |
| `/announce` (POST) | directory, proxied by Caddy on every domain, HTTP and HTTPS | no redirects for boards |
| `/install`, `/install/...`, `/connected`, `/upgrade`, `/hardware`, `/build`, `/different`, `/whofor`, `/kids`, `/teachers`, `/roadmap`, `/donate`, `/cover.svg`, `/static/*`, `/pix/*` | site; 301 from .net | sales and the installer |
| `/satellites` (new) | site | camera satellites now; door boxes and radio satellites coming |
| `/setup`, `/terminals`, `/dialing`, `/firstcall`, `/privacy`, `/forward`, `/forward-*`, `/sdcard`, `/lights`, `/camera`, `/skins` | docs repo, served at `.net/docs/<page>`; 301 from both the old .com and .net paths | sysop and caller guides |
| `/skins/<file>` (1.3.17 zips and pictures) | docs repo `skins/`, served by the directory; 301 from .com | they belong to the skins guide |

Pages that do not sit cleanly: `/build` (a funnel with a from-source section; kept on .com), `/upgrade` (a how-to bound to the installer; .com), `/camera` (a guide gated on which camera boards the installer offers; docs, the directory reads the site's firmware folder read-only for the gate), `/kids` and `/teachers` (persuasion; .com).

## 2. Code split (as built)

- `sitekit.py` (4,225 lines): page shell and stylesheet, wordmark, freedoms, glossary, the Markdown dialect with `BLOCKS`/`WRAPS`/`ART` registries, cta/next/cards, static and font readers, trusted-proxy rule, page cache, the firmware folder reader (`partition_table`, `set_layout`, `board_offers`, `BOARD_PREVIEWS`).
- Directory `server.py` (about 4,290 lines): announce, DB, list, badges, feed, JSON, data, how, rules, docs rendering, the guides' drawings, skins files, 301s.
- Site `server.py` (about 4,500 lines): front page, manifesto, author, installer, BOARDS, compare tables, satellites, the JSON-backed list, 301s. `deploy/fetch_release.py` moved here.
- The split is reproducible: a scratch script chunks the 1.3.18 monolith by top-level name and applies string edits that must each match exactly once.

## 3. Tests

- Baseline on 1.3.18: 969 passed, 0 failed.
- Directory: 524/0 (needs a docs checkout; `SELFTEST_DOCS`).
- Site: 636/0 (starts the directory beside it; `SELFTEST_DIRECTORY`, `SELFTEST_DOCS`).
- Docs `check.py`: 14/0.
- New checks include: the live list and its kept copy, directory down with and without a copy, escaping of names off the network, every 301, the Caddy/setup guards, the real 1.1.2 S3 `partitions.bin` (built by ESP-IDF 5.3.1's `gen_esp32part.py` from rel-1.1.2c `partitions_s3.csv`, committed as `tests/partitions-s3-1.1.2.bin`), S3 1.1.2 offsets (storage 0x780000), `unleashed_erase_below` on both S3 manifests from 1.1.2, and the dialog's erase rule.

## 4. The installer change for firmware 1.1.2

- Lives in **unleashed_site** (pushed) and in the directory branch's `sitekit.py` (not pushed). **Not** in the live unleashed_directory main.
- If v1.1.2 is tagged before the cutover, the live installer (1.3.18) would write an S3 1.1.2 `storage.bin` at 0x3C0000, inside the new `ota_1`, and offer Update. So either cut over first, or ask for a 1.3.19 backport onto the live monolith.
- The rule: BOARDS `erase_below: "1.1.2"` on the Waveshare; both its manifests from 1.1.2 carry `unleashed_erase_below`. The dialog erases unless the board says over Improv that it runs 1.1.2 or later. A Waveshare in download mode never answers, so **every** S3 install from 1.1.2 on erases. That is safe but costs accounts on a board already on 1.1.2 or later; /install says so, and recommends a backup first.

## 5. Firmware references (unchanged, all keep working)

- Announce default `http://unleashedbbs.net/announce` since before 1.0.0. `.com/announce` and `.org/announce` are proxied, not redirected.
- `unleashedbbs.com/setup` (CONFIG timezone note, `newsysop` screen, mkscreens) → 301 to `.net/docs/setup`. Next firmware batch: point them at `unleashedbbs.net/docs/setup`, but the timezone note is length-limited and the new address is 5 characters longer.
- "unleashedbbs.com lists them at /badges" (ANNOUNCE.md, system.cfg.example, announce.cpp comments) → 301 works; update to .net in the next batch.
- `release.yml` notes `unleashedbbs.com/install`: still right.

## 6. Droplet steps for Rob, in order

`$DIRECTORY` is the directory checkout that autopublish runs (`systemctl cat unleashed-directory-update` shows it). Clone the new repos beside it. unleashed_site is private: the droplet needs a read-only deploy key for it, as the directory had before 1.0.0.

1. Turn autopublish off.
2. Back up:
   ```sh
   sudo cp /etc/caddy/Caddyfile /root/Caddyfile.before-split
   sudo sqlite3 /var/lib/unleashed-directory/directory.db ".backup /root/directory-before-split.db"
   cat /etc/unleashed-directory/domains
   ```
3. Clone the guides and the site beside the directory:
   ```sh
   cd "$(dirname "$DIRECTORY")"
   sudo git clone https://github.com/rwmech/unleashed_documentation.git
   sudo git clone git@github.com:rwmech/unleashed_site.git
   echo "$PWD/unleashed_documentation" | sudo tee /etc/unleashed-directory/docs
   ```
4. Install the site. Nothing public changes yet, because the old Caddyfile has no import line:
   ```sh
   sudo ./unleashed_site/deploy/setup.sh unleashedbbs.com unleashedbbs.org
   sudo ./unleashed_site/deploy/update.sh
   curl -fsS http://127.0.0.1:8081/health
   curl -s -H 'Host: unleashedbbs.com' http://127.0.0.1:8081/directory | grep -c 'Communities online'
   ls /srv/unleashed_site/firmware
   ```
   The site's `setup.sh` copies the releases already in `/srv/unleashed_directory/firmware`, and `update.sh` fetches the newest.
5. Give the directory its one domain:
   ```sh
   echo unleashedbbs.net | sudo tee /etc/unleashed-directory/domains
   ```
6. Tell the agents to go. They push `dirsplit` (498811a) to unleashed_directory main.
7. Update the directory:
   ```sh
   sudo "$DIRECTORY/deploy/update.sh"
   ```
   It pulls 2.0.0 and the guides. It writes `/etc/caddy/sites/directory.caddy` and replaces the Caddyfile with the import, but only if every old name is served by a file in `sites/`. It reloads Caddy, which moves .com and .org to the site, and only then restarts the directory. It refuses outright if `domains` still holds unleashedbbs.com.
8. Check:
   ```sh
   for u in http://unleashedbbs.com http://unleashedbbs.org http://unleashedbbs.net \
            https://unleashedbbs.com https://unleashedbbs.net; do
     curl -s -o /dev/null -w "$u/announce %{http_code}\n" -X POST \
          -H 'Content-Type: application/json' -d '{}' "$u/announce"; done   # 400 each, never 3xx
   curl -sI https://unleashedbbs.com/setup | grep -i '^location'           # .net/docs/setup
   curl -sI https://unleashedbbs.net/install | grep -i '^location'         # .com/install
   curl -fsS https://unleashedbbs.net/docs/setup >/dev/null && echo docs ok
   curl -s https://unleashedbbs.com/directory | grep -c 'telnet://'
   curl -s https://unleashedbbs.org/ | grep -c 'What this is'
   ```
9. Autopublish back on, now running **both** `"$DIRECTORY/deploy/update.sh"` and `unleashed_site/deploy/update.sh`. The directory's also pulls the guides.

Rollback: `sudo cp /root/Caddyfile.before-split /etc/caddy/Caddyfile && sudo systemctl reload caddy` puts every domain back on 8080. To go back fully, `git -C "$DIRECTORY" reset --hard 60b121a`, put the three domains back in `domains`, and run its `update.sh`.

## 7. Risks

- Any 1.1.2 tag before the cutover (section 4).
- Every S3 install from 1.1.2 on erases, because download mode gives no version.
- The site's /directory is at most a minute old, and says so past three minutes.
- `sitekit.py` carries the site's styles in a public, GPL repo, as before. Trimming it per app is a follow-up.
- Decisions left to Rob: the site's LICENSE text (the README states all rights reserved, no licence granted), and whether `vendor/esp-web-tools/README.md` in the private repo keeps its GPL header.
