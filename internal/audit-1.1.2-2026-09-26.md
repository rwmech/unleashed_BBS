# Audit for 1.1.2: loop stalls and nano printf (2026-09-26, main at e656696)

Read-only audit by the code-review agent, saved by hand (that agent does not
write files). Nothing was built or run against a board; estimates are marked.

## Brief

Top ten stalls, worst case first:
1. announce: blocking DNS on the loop, in steady state too (seconds, nobody triggers it)
2. users.txt whole rewrite at every logoff (erase-bound, grows with accounts)
3. forums: per-record reopen when reading, unbounded (any reader)
4. SCREENS list (measured 1.22-1.28 s; staff)
5. FILES listings (measured 123-313 ms at 34 files; about 1 s at 200; any caller)
6. backup window GET: whole-set CRC read in one pass
7. handle prompt: users.txt parsed up to 3 times, unauthenticated
8. restore unpack: one file per pass up to 64 KB (inflate plus LittleFS write)
9. SCREENS INSTALL (measured 491 ms)
10. mail.dat whole rewrite, up to 36 KB (any caller)

printf formats to fix for nano: 1 call site, 3 conversions (camera.cpp:1609,
`%llu`). No `%f`, `%z`, `%j`, `%hh`, PRIu64 or positional arguments in src/.
The sscanf calls are C89 and safe.

Surprising:
- The SCREENS slowness is not a 1.1.1 regression: `whence`/`sdSeededStock`
  are identical to 1.1.0 (94aa16e); 1.1.1 only added the drive-light pulse.
- announce's "steady state never resolves" comment is false:
  announce.cpp:796 and :828 clear the address and :783 then resolves on
  the loop.
- A worker cannot fix erase-bound stalls. An SPI flash erase stops both
  cores whoever asks, so users.txt, mail.dat, calllog and system.cfg
  writes stall the loop from any task. The worker helps card work and
  reads only.
- `plat::taskStart` has one global trampoline (platform_esp32.cpp:1618-1626):
  two starts before the lower-priority task runs would run the second job
  twice. The shared runner has to own that.
- FILES.BBS is already written from both the camera worker and the loop,
  with `CONFIG_FATFS_FS_LOCK=0`.
- `fsInfoStale()` clears both partitions' figures, so every SCREENS INSTALL
  hands a userdata walk to whoever writes next.

## PART A: loop stalls

Context that governs every fix:
- The list machinery does a page per pass: `serviceList` (bbs.cpp:3185-3198)
  calls `listRow` while the timeline has more than 512 bytes and 16 frames
  free, so one pass runs up to about 20 rows. Every per-row cost is
  multiplied by that.
- Moving work to a worker is not free:
  - Flash erase: on the classic ESP32 an SPI flash write or erase disables
    the cache and parks the other core, so the loop stops for each sector
    erase whichever task asked
    (https://docs.espressif.com/projects/esp-idf/en/v5.3.1/esp32/api-reference/peripherals/spi_flash/spi_flash_concurrency.html;
    bbs_screens.cpp:393-395 already says so). LittleFS writes need smaller
    writes, not a worker.
  - LittleFS lock: one mutex per partition per operation. `esp_littlefs_info`
    / `lfs_fs_size` on the worker holds it for the whole traversal, so a
    caller playing a flash screen blocks behind it: it moves the stall and
    bounds it.
  - FatFs lock: reentrant, one mutex per volume per `f_*` call. A walk on
    the worker is many short holds (fine). `f_getfree` with a stale hint is
    one long hold.
  - No duplicate-open check: `CONFIG_FATFS_FS_LOCK=0`
    (sdkconfig.esp32dev:1111). Two tasks must never write the same card
    file (http://elm-chan.org/fsw/ff/doc/appnote.html#dup).
- The host misses all of this: Linux filesystem, no erase, no SPI, no
  8.3/LFN search, instant DNS, tiny data sets, no pass timing on the target.

### 1. announce: blocking DNS on the loop
- Where: src/plugins/announce.cpp:669-681 (`getaddrinfo`), from `start()`
  :1103 and `startPost()` :783. `startPost` resolves when `s.addr == 0`;
  :796 (connect error) and :828 (refused) zero it.
- Trigger: every CONFIG save, restore, boot; and any heartbeat after a
  failed resolve or connect, including login/logoff nudges via
  `onPresence`. Nobody, or the sysop.
- Operation: lwIP blocking DNS, retries (DNS_MAX_RETRIES 4, 1 s timer),
  3 servers. Seconds per failing lookup. Not measured: blackhole DNS on the
  bench.
- Fix: worker (lwIP sockets are thread-safe) or non-blocking
  `dns_gethostbyname` with a callback; keep the last good address instead
  of zeroing it on a connect error.

### 2. users.txt whole-file rewrite (users::rewrite)
- Where: src/core/users.cpp:365-415. Callers: `Bbs::saveCallStats`
  (bbs.cpp:2413-2427) at every account holder's logoff; PROFILE, PASSWORD,
  USER EDIT (bbs_users.cpp:394,416,473); `users::add` (3 full parses plus a
  rewrite, users.cpp:521-535); `markRecord` on first elevation
  (bbs_sysop.cpp:113-122); `rememberStaff` for co-sysops (:276-286);
  retire/purge.
- Operation: parse every block, write all to `.new`, rename. Erase-bound.
- Scales with accounts at about 450 B each: today a few KB (the measured
  hang-up 60-220 ms); at max_users 250 about 112 KB, about 28 sector
  erases, roughly 1+ s per logoff (estimate).
- Fix: structural, a worker cannot help. Move call stats (`calls`,
  `last_call`, `day`, `day_minutes`) to a fixed-record file indexed by id,
  updated in place. Cache `count()`.
- Incidental: users.cpp:410-412 falls back to `remove(live)` then rename,
  the shape CLAUDE.md forbids. On LittleFS only reached when rename failed
  for another reason (EBUSY, no space), and then it deletes every account.
  Settle by forcing a rename failure on the bench.

### 3. forums: per-record reopen, unbounded
- Where: `nextInSubject` forums.cpp:1002-1011 and `nextUnread` :1013-1024
  call `readRec` (:489-501: fopen, fseek, fread, fclose) per record, no cap
  (`scanSubjects` has `kScanMax`), from `readNext` (:1648-1681) on every
  Enter.
- Trigger: any reader in a subject whose next message is far ahead, or
  looking for unread under a subject filter.
- About 2-5 ms per card open (estimate): 1,000 intervening records is 2-5 s.
- Related: `scanSubjects` :933-981 reads up to 2,000 records per
  subject-list draw (about 200-400 ms at the cap, estimate); `liveUnread`
  :1038-1060 up to 2,000 records per forum with removals, for up to 16
  forums at FORUMS entry (:2355-2361); `writePtr` :684-707 writes the card
  on every message read.
- Fix: open the index once per walk; cap records per pass and resume; read
  pointers in RAM per slot, written at leave/logoff. A worker also fits
  (card reads).

### 4. SCREENS list (measured 1.22-1.28 s)
- Where: src/core/bbs_screens.cpp:295-360 (`rowScreens`). Per row:
  `nthScreen` (:122-150) re-walks flash and card folders from the start;
  `whence` (:155-174) runs three times, each: stat the card copy, call
  `sdSeededStock` (sd.cpp:813-823), which reads the whole `.seeded`
  manifest (`seededHash`, sd.cpp:302-319) and hashes the whole card screen
  (`fileHash`, sd.cpp:283-293, 64-byte freads), else stat flash.
- Trigger: any staff (CF_STAFF, bbs_shell.cpp:299).
- Not a 1.1.1 regression.
- Fix, Rob's shape: the worker builds the table once (64 names by 3 cells
  of from/size, about 2 KB, one table not per session): manifest read once,
  each folder walked once; the loop draws rows from the table. Pure reads,
  so the worker removes it.
- Cheaper interim: a time budget in `serviceList` (stop at about 10 ms,
  carry on next pass), which also caps 5 and 23. One SCREENS row is still
  about 30-60 ms, so the budget alone is not enough here.

### 5. FILES listings (measured 123-313 ms, Screens area, 34 files)
- Where: src/plugins/files.cpp:1081-1207 (`rows`). Per row: `disk::dir` and
  readdir to row i (:1098-1139, O(n)); `stat` (:1159, FAT directory search);
  `findDesc` (:562-580) reads FILES.BBS from the top. Photos:
  `photoEntry` (:437-443) runs `plat::sdList` over Photos and every handle
  folder per row.
- Trigger: any caller with the area's read level.
- About 1 s per page at 200 files (estimate).
- Fix: one walk per page into a shared page cache claimed with
  `claims::seize` (like the forums' subject table); FILES.BBS read once per
  page; or the worker builds the page; the `serviceList` budget as interim.

### 6. Backup window download: CRC scan in one pass
- Where: backup.cpp:460 calls `ZipExport::scan` (ziparc.cpp:562-590):
  `snapshotUsers` (:823-840, a LittleFS write) and `measure` (:455-466)
  reading every entry in full.
- Trigger: a LAN GET while the sysop has the window open.
- Up to BBS_ZIP_TOTAL_MAX 262,144 bytes. Estimate 0.3-1 s.
- Fix: CRC one file per pass before the 200, or on the worker. The snapshot
  copy stays erase-bound (one file).

### 7. Pre-login users.txt parses (unauthenticated, repeatable)
- Where: bbs.cpp:1476 (`users::lookup`, full parse on an unknown handle),
  :1506 and :1514 (`users::count`, full parses); then `readForLogin`
  (:1769-1775) and 1,000 SHA-256 rounds (users.cpp:150-161, software).
- Trigger: anyone at the handle prompt, continuously, on 10 lines.
- Tens of ms today; about 100-300 ms per unknown handle at 250 accounts
  (estimate).
- Fix: a RAM handle index (hash to id/offset, about 2 KB static for 250)
  and a cached count, invalidated in `rewrite`. The hash could go to the
  worker (fixed, about 10-20 ms, estimate).

### 8. Restore unpack (upload and RESTORE SD check)
- Where: `ZipImport::step` (ziparc.cpp:1246-1266) runs `extract` (:1166):
  inflate up to 65,536 bytes with ROM tinfl, CRC, write to
  `<userdata>/.staging` in one pass; `users::validateFile` on staged
  users.txt (:1226); receipt forces a userdata walk (`fsInfoStale()` plus
  `userInfo`, backup.cpp:497-498 and ziparc.cpp:893-894).
- Trigger: sysop; runs with callers on (the quiet hold covers apply only).
- About 0.5-1 s for 64 KB (estimate, erase-bound for the write).
- Fix: inflate, CRC and validate on the worker; write in 8 KB slices like
  the card writer.

### 9. SCREENS INSTALL (measured 491 ms)
- Where: plan end (bbs_screens.cpp:686-710): `plat::fsInfo` (walks the
  256 KB screens partition when stale), then `mkdir .stock` (a LittleFS
  metadata commit, possibly compaction). Copy's final pass (:776-821):
  `endScreens`, two LittleFS renames, the `.stock` marker, and
  `sdSeededMark` (sd.cpp:831-878: read manifest, temp, rename, EEXIST
  remove-and-rename fallback). Plan per entry: `cardEntry` re-walks from 0
  (:447-467, O(n^2) over the job), plus `sdSeededStock` and `sameFile`
  reading both files whole (:470-485).
- Lean: the 491 ms is the fsInfo walk plus the mkdir. The cached figure is
  dropped by every install's `finish()` (:612) and every restore. Settle
  with `plat::micros()` around each.
- Collateral: `fsInfoStale()` (platform_esp32.cpp:171-174) clears the
  userdata figure too, so the next plugin write or DASH frame pays a
  userdata `lfs_fs_size` walk (about 85 ms). Give it a partition argument.
- Fix: plan, manifest mark and card reads on the worker. The rename step
  stays on the loop in the same pass as `endScreens`: esp_littlefs refuses
  to rename over an open file, and a caller could open the screen between
  the two if split across tasks.

### 10. mail.dat whole rewrite
- Where: src/plugins/chat.cpp:1122-1163 (`mailRewrite`), then `mailIndex`
  (:1016-1036) re-reads it.
- Trigger: any caller sending mail, R/S/D, a reply, expiry.
- Up to 64 x 564 = 36,096 bytes; about 300-450 ms (estimate, erase-bound).
- Fix: fixed 564-byte records, so update in place with `r+b`. littlefs
  commits a file's changes atomically at sync/close (littlefs DESIGN.md,
  power-loss resilience), so temp-and-rename buys nothing on userdata.

### 11. CONFIG save (measured 216-338 ms, plus item 1)
- Where: bbs_sysop.cpp:2539 (`syscfg::write`, about 11 KB), then
  `configReloadAll` (:2556), then `restartPlugins` (:2590-2616):
  `plugins::begin` (plugin.cpp:302-313) reads the whole system.cfg once per
  plugin (`scan`, :106) and again in each `forEachKey` (about 24 full
  reads); `plat::heap()` walks the heap per plugin; chat `start`
  (chat.cpp:3254-3310): `mailIndex`, `mailMarkSysops` (a users.txt pass),
  `loadBans`; files `start` (files.cpp:756-846): stat and mkdir per area,
  `recountPending` walks every `.pending`; forums `readHeader` per forum
  (forums.cpp:796-824); announce DNS.
- Trigger: sysop.
- Fix: one system.cfg pass dispatching to every plugin's `readKey`;
  restart only plugins whose section changed; `heapFree()` not `heap()`;
  DNS off the loop.

### 12-23. Smaller, in rough order
12. Hang-up (measured 60-220 ms, any caller's): bbs.cpp:835-878:
    `onLogoff` hooks, `calllog::append` (calllog.cpp:178-213, LittleFS
    in-place write), card mirror (calllog.cpp:137-175: mkdir, append,
    fflush, close; SD write-busy up to 250 ms), `saveCallStats` (item 2),
    `plat::heap()` at :876 for a log line. Fix: mirror on the worker from a
    small CallRec queue; `heapFree()`.
13. CONFIG open (measured 130-173 ms): `cfgFileValue` (bbs_sysop.cpp:1305)
    opens and scans all of system.cfg per field; `cmdConfig` (:2131-2160)
    calls it up to 16 times plus wifi and tz. Fix: one pass fills the page.
14. Elevation (measured 90-138 ms): `elevate` (bbs_sysop.cpp:352-):
    `markRecord` parse (rewrite on first elevation); `sysopLastSave` (:206)
    then `chat::sysopChanged` -> `mailMarkSysops` (another users.txt pass);
    `staffArrival` (bbs.cpp:2714): `ringNotes` reads and clears rings.txt,
    `nightlyNotice`. Co-sysops also `rememberStaff` (a rewrite).
15. The minute userdata walk: `plugins::path` -> `freeBytes`
    (plugin.cpp:232) -> `userInfo`, refreshed when older than 60 s
    (platform_esp32.cpp:156-178). First plugin write or DASH frame each
    minute pays `lfs_fs_size`, about 85 ms. Fix: the worker refreshes and
    publishes it atomically.
16. Login (measured 65-77 ms): lookup parse plus 1,000 SHA-256 rounds;
    `restoreStaff` (bbs_sysop.cpp:288, second parse); for the sysop
    `isSysopAccount` (bbs.cpp:1944, third parse); `onLogin` hooks.
17. BACKUP SD and the nightly (backup.cpp:807-822): 8 KB per pass to the
    card; usually tens of ms, SD write-busy spikes; callers may be on. A
    worker fit. Hazard: users.txt read across passes (`Src::File`, not a
    snapshot, ziparc.cpp:519-524) while a logoff can rename a new one over
    it; the zip may carry a torn users.txt (`validateFile` would catch it at
    restore). Settle on the bench with a logoff mid-nightly.
18. XMODEM/YMODEM: 1 KB card I/O per block per pass (files.cpp:1706-1740);
    SD write spikes up to 250 ms. Any caller transferring.
19. SD MOUNT and pin changes: blocking mount plus `seedScreens`
    (sd.cpp:391-), hashing every stock and card screen and re-reading the
    manifest per file (O(n^2)). Sysop; documented as a pause.
20. Forum post: `appendMessage` (forums.cpp:587): body append, index
    append, header rewrite. Any poster.
21. Plain-ASCII USERS list: `rowUsers` (bbs_users.cpp:902) runs
    `users::at(i)` per row, O(n^2) per page. Staff with PERM_USERS.
22. LAST past `kRecent`: a fopen per row (calllog.cpp:241-257). Under 50 ms.
23. Screen playback probes: `ScreenPlayer::find` (screens.cpp:118-137),
    up to 2 folders x N extensions; failed FAT opens are directory searches.
    A few ms.

### Thread-safety for the shared runner
- The trampoline is one global: `plat::taskStart` stores fn/arg in a
  single `g_worker` (platform_esp32.cpp:1618-1626) read when the new task
  first runs, three priorities below the loop. Two starts in one pass
  overwrite the first. Safe today only because camera gates on `jobBusy()`.
  The runner: one task with a job slot, or pass the argument through
  `pvParameters`.
- Job state: the camera's discipline (atomic phase set last by the worker,
  results in the job struct, the loop reads only after DONE;
  camera.cpp:409-465, 981-1025). SCREENS INSTALL's `g_in` is plain statics.
- Session pointers: never hold a `Session*` across passes; carry node id
  and re-find it (camera `waiter()`, camera.cpp:1209-1219; SCREENS
  INSTALL's `who` plus `CardJob` plus `loggedIn`, bbs_screens.cpp:581-582).
  No generation counter; add one if the runner outlives hang-ups.
- FILES.BBS has two writers today: the camera worker
  (`photoDesc`/`photoDescDrop`, camera.cpp:975, 757, into files.cpp:594-653,
  temp `.ctmp`) and the loop (DESC and approve, files.cpp:1389-1417, 2500,
  temp `.tmp`). Last rename wins and a description is lost; the EEXIST
  fallback removes the live file while the other side may have it open,
  with FATFS_FS_LOCK=0. Serialize through the runner.
- Unmount guards: SD UNMOUNT and pin changes check `camera::busy()` only
  (sd.cpp:504, 680). Make them check the runner.
- Free-space statics `g_userFig`/`g_dataFig` (platform_esp32.cpp:151) are
  multi-word and unsynchronized; if the worker takes them, publish from the
  job result.
- The manifest is shared between SCREENS list and SCREENS INSTALL
  (`sdSeededStock` reads while `sdSeededMark` replaces, a FAT
  remove-then-rename window): one job lock for both.

## PART B: nano printf (CONFIG_NEWLIB_NANO_FORMAT=y)

What nano drops (newlib README, `--enable-newlib-nano-formatted-io`,
https://sourceware.org/git/?p=newlib-cygwin.git;a=blob;f=newlib/README):
C89 formatted I/O only; `io-c99-formats`, `io-long-long`,
`io-long-double`, `io-pos-args` ignored, so `%ll`, `%hh`, `%j`, `%z`,
`%t`, `%n$` are gone. Float moves to weak `_printf_float`/`_scanf_float`,
not linked unless requested with `-u`. ESP-IDF's option help (newlib
Kconfig, v5.3.1) says it does not support 64-bit integer formats and C99
features such as positional arguments (recalled wording; check at the tag).

Likely failure mode (lean): nano parses a single `h`/`l`/`L`, so in `%llu`
the second `l` becomes the conversion, printed as-is, consuming no
argument, so later arguments shift. Settle with one bench print or
nano-vfprintf_i.c at the IDF's newlib tag.

Sites in src/: 1 call, 3 conversions.
- src/plugins/camera.cpp:1609:
  `snprintf(buf, sizeof(buf), "Oldest kept %04llu-%02llu-%02llu", o / 10000, (o / 100) % 100, o % 100);`
  Reaches the CAMERA status screen (staff). Likely prints `000lu-0lu-0lu`.
  Fix: YYYYMMDD fits 32 bits:
  `unsigned d = static_cast<unsigned>(g_stats.oldest / 1000000ull);` then
  `"%04u-%02u-%02u", d / 10000, (d / 100) % 100, d % 100`.

Checked clean: no `%f`/`%e`/`%g`/`%a` in any printf (the `%a %d %b` hits
are strftime via `clk::fmt`); no `%z`, `%j`, `%hh`, `%t`, PRI*/SCN* or
positional arguments; every printed 64-bit value is already cast down
(camera.cpp:1539-1542 `mb()`, backup.h:288-296 `sdCardGB`); announce JSON,
users.txt, system.cfg, the caller log mirror and forum records use
`%u`/`%lu`/`%s`/`%0Nlx`.

scanf family: backup.cpp:432 (`%7s %63s`), serialbridge.cpp:325
(`%11s %7s`), example.cpp:68 (`%u`), all C89. `strtoull` at
sysconfig.cpp:408 is unaffected. No `strtof`/`atof`.

Third-party: managed_components/espressif__esp32-camera/sensors/gc0308.c:227
and gc2145.c:238 pass `%f` to ESP_LOGI; under nano the ratio prints empty
(console only). Worth knowing when reading a GC0308 bench log.
