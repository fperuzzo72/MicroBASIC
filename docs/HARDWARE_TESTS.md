# Pending hardware tests

Things implemented but not yet exercised on a real X4, in the order they
landed. The pattern this project has settled into is a run of changes made
and verified as far as they can be off-device (host tests, browser-driven
mock backends, build checks), then one hardware session that goes through
this list.

Tick things off and delete them once confirmed; anything that fails moves to
`DEVELOPMENT_LOG.md` with what was observed.

---

## 1. Wrapped logical lines in the screen editor

The fix that replaced `logicalLineStartRow` with per-row continuation flags
(so Enter reads the whole logical line no matter where in it the cursor is).

- [ ] Type a line longer than the screen is wide (>48 chars on SCREEN 1) as a
      numbered program line, e.g. `10 PRINT "aaaa...."` past the right
      margin. It should wrap onto the next row while typing.
- [ ] Press Enter. `LIST` should show it stored complete, not truncated.
- [ ] `LIST` it again, then arrow **up onto the second (wrapped) row** of that
      line, change a character, press Enter. The whole line should be
      re-stored with the edit — **this is the case that was broken before**;
      it used to store only the fragment from the second row onwards.
- [ ] Same test with the cursor on the *first* row of the wrapped line.
- [ ] Check a wrapped line that reaches the bottom of the screen and scrolls:
      the part that scrolled off is gone (expected), no crash or garbage.

## 2. Program store (packed buffer)

Replaced the fixed 60-line array. Logic is covered by `test/run_tests.sh` on
the host; what needs a device is that it behaves the same there.

- [ ] Enter a handful of lines out of order (30, 10, 20) — `LIST` shows them
      sorted.
- [ ] Retype an existing line number with different text — it replaces.
- [ ] Type a line number alone — that line disappears from `LIST`.
- [ ] Enter more than 60 lines (the old ceiling) — should now be fine.
- [ ] Keep going until it refuses: expect `?Out of memory`, not a crash or
      silent loss.
- [ ] Try line number `0` and something above `65535` — expect
      `?Line number out of range`.

## 3. SAVE / LOAD

Names are now stored **lower case** and used exactly as typed (no forced
`.bas`), with a case-insensitive fallback on load.

- [ ] `SAVE "TESTE"` then `FILES` — the file should appear as `teste`
      (lower case, no extension).
- [ ] `NEW`, then `LOAD "TESTE"` — loads despite the case difference.
- [ ] `LOAD "teste"` — same result.
- [ ] Copy a file onto the SD card from a PC named `JOGO.BAS`, then
      `LOAD "jogo"` — the case-insensitive scan should still find it.
- [ ] `LOAD "naoexiste"` — expect `?File not found`.
- [ ] Put a non-program text file in `/MicroBASIC/programs/` and LOAD it —
      expect `?No program lines in file`, **not** a silent empty program.
- [ ] Open a saved file on a PC — it should be readable plain text, one
      `10 PRINT ...` per line.

## 4. VC — the program picker

New full-screen file selector, opened by typing `VC`. Never run on hardware
at all yet, so this one needs the most attention.

- [ ] `VC` with no saved programs — shows "(no programs saved yet)" and Esc
      returns to the terminal.
- [ ] `VC` with several programs — list appears, first entry highlighted.
- [ ] Arrow up/down moves the highlight; left/right jump by a whole column.
- [ ] PgUp/PgDn page; Home/End jump to first/last.
- [ ] Enter loads the highlighted program, returns to the terminal, and
      prints `Loaded <name>`. `LIST` confirms it's really in memory.
- [ ] Esc returns **without** loading, and the terminal content from before
      is still there (VC draws to the panel, not into the grid, so nothing
      should have been overwritten).
- [ ] **Layout across SCREEN modes** — this is the part most likely to be
      wrong, since it's all computed rather than laid out by hand. Run `VC`
      in each of `SCREEN 0`, `1`, `2`, `3` and check: the title bar and the
      bottom bar span the full width, the columns don't overlap or run off
      the right edge, and long file names are truncated rather than
      overflowing into the next column. Expect 1 column on SCREEN 0, 2 on
      SCREEN 1 and 2, 3 on SCREEN 3.
- [ ] With more programs than fit one page, check scrolling by page works
      and the highlight stays visible.
- [ ] Physical d-pad also navigates (Confirm = load, Back = quit).

## 5. Web file manager tabs

Verified in a browser against a mock backend — layout, tab switching,
per-tab requests and download links are known good. What that could **not**
test is the device side actually serving the new collection parameter.

- [ ] Start Sync, open the page: two tabs, Notes selected.
- [ ] Notes tab lists `.txt` notes and **not** any `.bak` files.
- [ ] Programs tab lists what's in `/MicroBASIC/programs/`, including files
      saved without an extension.
- [ ] Download from each tab — correct file, correct contents.
- [ ] Upload a `.txt` on the Notes tab; confirm it appears on the device's
      own file browser too.
- [ ] Upload a program on the Programs tab, then `LOAD` it on the device.
- [ ] Delete from each tab — removes the right file, and deleting a note
      also clears its `.bak`.
- [ ] Upload something too large on each tab — expect a clear size error,
      not a truncated file.

## 6. TinyBasic interpreter — live

My-Basic is gone from the binary. The interpreter now owns program storage and
every classic command; only `MENU`, `VC` and `SCREEN` are still the firmware's.

Confirmed working on hardware already: program entry, `LIST` with correct line
numbers, `RUN`, Ctrl+C to break, variables persisting between direct-mode
statements (`A=5` then `PRINT A+B`), `CLS` clearing the screen.

Confirmed on hardware since: `CLS`, `SCREEN` switching, `MENU`, `CATALOG`,
`FILES`, `LOAD`, loading via `VC`, and `LIST` with correct line numbers.

Fixed after that round, needs retest:

- [ ] `SAVE "x"` now actually writes the file. It was producing 0 bytes
      because SAVE works by setting the output channel to `OFILE` and
      *printing* the program through `outch()`, and `outch()` ignored the
      channel and always wrote to the screen. Check the file has content and
      is readable plain text on a PC, and that `LOAD` brings it back.
- [ ] Backspace at column 0 no longer walks up into the previous row unless
      that row is genuinely the wrapped continuation of it. Type two separate
      lines, put the cursor at the start of the second, press Backspace: the
      line above must be left alone.
- [ ] `SCREEN n` and `FILES` no longer leave a blank line after running.

Still to check:

- [ ] `NEW` empties the program; `LIST` after it shows nothing.
- [ ] `VC` returning to the terminal: the typed "VC" now stays on screen, and
      there should be no blank line after "Loaded ...".
- [ ] Long `RUN` output still repaints as it goes (the runtime's `byield()`
      flushes the display every 500ms).

**Known limitations, not bugs:**

- `LEFT$`/`MID$`/`RIGHT$` work on string *variables*, not literals:
  `A$="teste": PRINT LEFT$(A$,3)` works, `PRINT LEFT$("teste",3)` gives
  `Args Error`. This interpreter does in-place string operations, so those
  functions return a reference into a variable and have nothing to point at
  in a temporary literal. `LEN`, `ASC` and `CHR$` take literals fine.
- `INPUT` does not work yet: it pulls characters through the runtime's
  `inch()`, and line editing here belongs to the screen editor. Needs the
  same "hand a finished line over" bridge that Enter already uses.
- `LIST` no longer paginates with `MORE?` — it scrolls, as MSX did.
- `LIST n` lists **only** line n; use `LIST b,e` for a range. Deliberate:
  it matches MSX, where "from n onwards" was a separate form, and makes the
  single-argument case unambiguous.
- `CONT` after a break often fails with a syntax error. Diagnosed on the host
  harness and it is an interpreter bug, not an integration one: the execution
  loop keeps one token lexed ahead of `here`, and the break path discards it,
  so `CONT`'s `nexttoken()` re-reads from `here` and *skips a token*. When the
  skipped token is a line number nothing is lost (those cases resume fine);
  when it is a statement keyword, execution resumes inside that statement's
  arguments and errors. Reproducible deterministically: breaking at
  `here`=3 or 12 resumes cleanly, at 4 or 13 gives "10:"/"20: Syntax Error".
  Fixing it means saving `token` at break and restoring it on `CONT` --
  a change to interpreter internals, deliberately not made unilaterally.
  Use `RUN` to restart in the meantime.

RAM is at ~55%, flash at ~27.5% (down from 31.7% once My-Basic's code left the
binary). BLE needs a 20KB contiguous allocation at connect time, so RAM is the
number to keep watching.

## 7. Round four: output speed, EXIT, boot line

- [ ] A `PRINT`/`GOTO` loop now prints at a usable speed -- output arrives in
      batches roughly every 0.4s plus one refresh, not one line every few
      seconds. This was our own throttle bug (see DEVELOPMENT_LOG.md); the
      remaining floor is the panel's ~700ms refresh, which no software change
      removes.
- [ ] A loop that computes without printing (e.g. `FOR I=1 TO 5000: NEXT`)
      no longer refreshes the display at all while it runs.
- [ ] `EXIT` leaves the screen editor exactly as `MENU` does.
- [ ] The boot screen shows `MicroBASIC v0.3 for XTeink X4` above the
      interpreter's greeting.
- [ ] Ctrl+C / Esc still break a running loop (the yield path changed).

## 8. Round five: keyboard, LOCATE, menus, sync

- [ ] `Break in <line>` is printed when Escape or Ctrl+C stops a program.
- [ ] `DIR` lists the SD card exactly as `FILES` and `CATALOG` do.
- [ ] `10 LOCATE 5,3` then `20 PRINT "X";` puts the X at column 5, row 3 --
      no stray characters anywhere.
- [ ] `10 GET K` / `20 IF K=0 THEN GOTO 10` / `30 PRINT K` reports the key
      code; arrows give 28/29/30/31 (right/left/up/down).
- [ ] `examples/pacman.bas`: type `SCREEN 1`, then `RUN`. The maze draws, the
      arrows move the player, the ghost chases, the score counts up. This is
      the speed test -- how playable it feels is the answer.
      Upload it through Sync's Programs tab, or use `VC` after uploading.
- [ ] Main menu: seven entries -- MicroBASIC, Browse Programs, New Program,
      Browse Files, New Note, Settings, Sync -- plus the OTA apps.
- [ ] `Browse Programs` lists `/MicroBASIC/programs` and its header says
      "Programs"; `Browse Files` lists the notes folder and says "Notes".
- [ ] `New Program` saves into `/MicroBASIC/programs` as `<title>.bas`, and
      `LOAD "<title>.bas"` at the BASIC prompt finds it.
- [ ] `New Note` still saves into the notes folder as before.
- [ ] Neither listing shows `.tmp` or `.bak` files.
- [ ] Sync scans for networks. If it still fails, the message now says which
      failure it was -- "Radio busy (heap NNK)" means the WiFi stack could
      not allocate, "Scan failed" means the radio returned nothing.

## 9. Round six: filenames and the reclaimed heap

- [ ] `New Program`: the field is prefilled `untitled.bas` and the header
      says "Edit Filename". Typing `novo.bas` produces a file called exactly
      `novo.bas`; typing `novo` produces `novo.bas`; typing `novo.b`
      produces `novo.b`.
- [ ] `Browse Programs` shows full filenames, extensions included.
- [ ] `LOAD "novo.bas"` at the BASIC prompt opens what `New Program` saved.
- [ ] Renaming a file and confirming it *unchanged* leaves the name alone
      (it used to become `name_2`).
- [ ] `New Note` / `Browse Files` are unchanged: titles, not filenames.
- [ ] Sync: 11.4KB of static RAM came back, so the scan may now work. If it
      still fails, the message reports the largest free block -- that number
      is the thing to report.
- [ ] Regression from the removals: `LIST` of a long program scrolls (no
      `MORE?` prompt, which is intended), `CATALOG`/`FILES`/`DIR` still list,
      `SAVE`/`LOAD` still work, and the screen editor still takes typing.

## 10. Round seven: sync

- [ ] The file page loads at a normal speed, not in tens of seconds.
- [ ] The Notes tab lists the notes on the card.
- [ ] Clicking "BASIC programs" switches tabs and lists /MicroBASIC/programs.
- [ ] Uploading `examples/pacman.bas` through the Programs tab puts it where
      `LOAD "pacman.bas"` finds it.
- [ ] The session survives a few minutes of reading without disconnecting.
- [ ] **Password saving**: on connecting with a typed password, does the
      "Save password? Enter/Up: Yes  Down/Esc: No" screen appear at all? If
      it does and Enter is pressed, is the network offered without asking
      next time? This one has no diagnosis yet -- what is needed is the
      answer to that first question.
- [ ] Does the BLE keyboard still drop out? 27.8KB of static RAM came back
      this round, and the suspected cause is the connect task failing to get
      its 20KB contiguous allocation.

## 11. Round eight: prompts, cursor, Pacman

- [x] Sync no longer reboots on entry (the abort was WiFi.setSleep(false),
      illegal with BLE enabled -- removed).
- [x] "Save password?" stays on screen until *you* answer it, and answering
      Enter means the network connects without asking next time. **Confirmed:
      saved, and the second entry auto-connected without scanning at all.**
- [x] The page loads fully -- tabs work, files list.
- [x] No cursor block next to the sprites while Pacman runs.
- [x] Pacman: one arrow press keeps him walking until he hits a wall.
- [x] The ghost patrols rather than pacing back and forth in place.

`-DRELEASE_BUILD` is back on now that the sync flow is confirmed.

## 12. Round nine: INPUT (flashed)

- [ ] `10 INPUT "NOME? ";A$` / `20 PRINT "OI ";A$` -- the prompt appears
      immediately (not after a delay), typing echoes, Enter accepts.
- [ ] Backspace erases inside INPUT and stops at the start of the answer.
- [ ] `INPUT N` (numeric): a non-number re-asks; a number assigns.
- [ ] `INPUT A$` with no prompt string shows `? `.
- [ ] Esc / Ctrl+C during an INPUT stops the program rather than being
      stored as text.
- [ ] Arrow keys pressed during INPUT are ignored, not inserted.
- [ ] After Enter, whatever the program prints next starts on the line
      below the answer.
- [ ] A long-running program with an INPUT in a loop still repaints and
      does not trip the watchdog.

Free A/B for the phantom RIGHT, costing nothing to try: the sync screen
pins the CPU at 80MHz with light sleep off for as long as it is open. If
the spurious presses stop while that screen is up and come back on leaving
it, the power management is confirmed as the cause.

## 13. Regression check

Quick pass over things that already worked, since these changes touched
shared code (`screen_editor`, `input_handler`, `ui_renderer`, `wifi_sync`).

- [ ] Typing, backspace, arrows, Home/End/PgUp/PgDn in the screen editor.
- [ ] `RUN` a small program; Ctrl+C or Esc still breaks a loop.
- [ ] `SCREEN 0..3` still switch and redraw correctly.
- [ ] `MENU` still exits to the main menu, and physical Back still works.
- [ ] The prose editor (New Program) still opens, edits and saves.
- [ ] BLE keyboard still connects on boot.
