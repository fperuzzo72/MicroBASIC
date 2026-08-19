// Runtime for Stefan Lenz's IoT BASIC interpreter (editor/lib/TinyBasic,
// fetched by patches/tinybasic/fetch.sh -- not vendored, see that script).
//
// The interpreter is written against a runtime contract declared in its
// runtime.h: character I/O, a filesystem, timing, and a pile of peripheral
// hooks. Upstream ships two implementations of that contract (a POSIX one and
// a large Arduino one full of display/keyboard/sensor drivers); this is a
// third, mapping it onto what this firmware already has -- the SCREEN 0-3
// character terminal in screen_editor.cpp and the SD card via SDCardManager.
//
// The contract was determined empirically rather than from documentation:
// compiling basic.c alone and diffing its undefined symbols against what the
// upstream runtime defines. That yields 76 names, of which roughly half are
// peripherals this project doesn't have (GPIO, MQTT, RTC, printer, EEPROM)
// and are stubbed at the bottom of this file. Disabling the corresponding
// feature flags in language.h (patches/tinybasic/01) doesn't remove them --
// they're referenced from dispatch tables compiled in regardless -- so they
// still have to exist, they just never get called.
//
// This is live: the interpreter now owns program storage and every classic
// command (LIST/RUN/SAVE/LOAD/NEW/CLS/...). Only MENU, VC and SCREEN are still
// handled by the firmware, being this device's rather than the language's.
// See docs/DEVELOPMENT_LOG.md.

#include "config.h"
#include "screen_editor.h"
#include "input_handler.h"

#include <Arduino.h>
#include <SDCardManager.h>

#include <cstdio>
#include <cstring>

#include "tb_interp.h"

// ---------------------------------------------------------------------------
// State the interpreter expects the runtime to own (declared extern in
// runtime.h). Initial values follow upstream's own runtime.
// ---------------------------------------------------------------------------

int8_t id = ISERIAL;      // current input device
int8_t od = OSERIAL;      // current output device
int8_t idd = ISERIAL;     // default input device
int8_t odd = OSERIAL;     // default output device
int8_t ioer = 0;          // last I/O error
uint8_t blockmode = 0;
uint8_t breaksignal = 0;
uint8_t bsystype = SYSTYPE_UNKNOWN;
uint8_t sendcr = 0;
uint8_t vt52active = 0;

// Per-device output column, which is how the interpreter implements PRINT's
// comma tab stops. Keeping this accurate is what makes `PRINT A,B,C` line up
// in columns the way a classic BASIC does, so it's maintained in outch()
// rather than left at zero.
uint8_t charcount[5] = {0, 0, 0, 0, 0};

// Open file handles. Declared up here rather than next to the filesystem
// functions below because outch() needs `ofile`: SAVE writes by switching the
// output channel to OFILE and printing through outch().
static FsFile ifile;      // open for reading
static FsFile ofile;      // open for writing
static FsFile rootDir;    // directory walk
static FsFile rootEntry;  // current entry in that walk
static char rootNameBuf[MAX_FILENAME_LEN];

// Set whenever something reaches the terminal; read by byield() so an
// unchanged screen isn't refreshed. Defined next to byield().
static bool termDirty;

// Partial VT52 escape sequence, see outch().
enum : uint8_t { VT_NONE, VT_ESC, VT_ROW, VT_COL };
static uint8_t vtState = VT_NONE;
static int vtRow = 0;

// ---------------------------------------------------------------------------
// Console I/O -> the SCREEN 0-3 terminal
// ---------------------------------------------------------------------------

// Writes one character to whichever channel `od` currently selects. Getting
// this wrong is not cosmetic: SAVE works by setting `od = OFILE` and then
// *printing* the program through outch(), so an outch() that always wrote to
// the screen produced a listing on the terminal and a 0-byte file on the SD
// card -- which is exactly what happened before this dispatched on `od`.
//
// Deliberately unlike upstream's own outch(), this does not call byield() per
// character. Upstream does that "for fuzzy OSes"; here byield() carries a
// vTaskDelay and a display flush, and paying that per character would make
// output crawl.
void outch(char c) {
  if (od == OFILE) {
    // Raw passthrough: a saved program is plain text and must keep its real
    // line endings, so none of the terminal translation below applies.
    if (ofile) ofile.write((const uint8_t*)&c, 1);
    return;
  }
  if (od != OSERIAL) return;  // printer, wire, radio, MQTT: nothing attached
  termDirty = true;

  // LOCATE is not a runtime call upstream: xlocate() implements it by writing
  // a VT52 "ESC Y row col" sequence through outch(). A runtime that doesn't
  // decode that gets four stray characters printed instead of a cursor move,
  // which is what happened before this. Decoding this one sequence is what
  // makes screen-oriented BASIC possible here -- a program can repaint in
  // place instead of scrolling, which on e-ink is the difference between a
  // game and a listing. Both operands are biased by 32 and 1-based, so
  // LOCATE 1,1 is the top-left cell.
  if (vtState != VT_NONE) {
    switch (vtState) {
      case VT_ESC:
        // Only cursor addressing is decoded. Anything else is swallowed
        // rather than printed: a stray escape is never something the user
        // meant to see on a character grid.
        vtState = (c == 'Y') ? VT_ROW : VT_NONE;
        return;
      case VT_ROW:
        vtRow = (int)(unsigned char)c - 32;
        vtState = VT_COL;
        return;
      default:
        screenEditorSetCursor(vtRow, (int)(unsigned char)c - 32);
        charcount[OSERIAL] = (uint8_t)screenEditorGetCursorCol();
        vtState = VT_NONE;
        return;
    }
  }
  if (c == 27) { vtState = VT_ESC; return; }

  if (c == '\r') return;  // terminal treats '\n' alone as end of line
  if (c == '\n') {
    screenEditorStartNewInputLine();
    charcount[OSERIAL] = 0;
    return;
  }
  // Form feed is how the interpreter implements CLS -- its TCLS case is
  // literally `outch(12)`. Handling it here means CLS works without being
  // intercepted as a special case the way it had to be under My-Basic.
  if (c == 12) {
    screenEditorReset();
    for (int i = 0; i < 5; i++) charcount[i] = 0;
    return;
  }
  const char s[2] = {c, '\0'};
  screenEditorTermPrint(s);
  charcount[OSERIAL]++;
}

void outs(char* s, uint16_t l) {
  for (uint16_t i = 0; i < l; i++) outch(s[i]);
}

// Single-key input. Not used for typing whole lines -- those are injected
// into the interpreter's buffer by tb_bridge.cpp -- but it is exactly what
// BASIC's GET statement and its @C special variable read, and those are how a
// program polls the keyboard without stopping. Non-blocking by design: GET
// stores 0 when nothing was pressed, which is what lets a game loop keep
// running while nobody is touching the keys.
char inch() { return inputReadProgramKey(); }

// The interpreter checks this after *every* statement while a program runs,
// and treats BREAKCHAR as "stop". That's the natural place to hook this
// firmware's own break keys, so Escape or Ctrl+C interrupts a running program
// without needing anything like the stepped-handler workaround My-Basic
// required (see docs/DEVELOPMENT_LOG.md).
//
// Safe to repurpose because normal input never comes through here: lines are
// injected whole into the interpreter's buffer by tb_bridge.cpp, so checkch()
// is only ever reached on the break path.
char checkch() {
  if (!inputConsumeBreakPending()) return 0;

  // Announce it. The interpreter's own break path is silent -- it just stops
  // and returns to the prompt -- which on a screen that is already full of a
  // program's output leaves nothing to distinguish "I stopped it" from "it
  // finished". Printing here rather than after tbExecuteLine() returns is
  // deliberate: `st` still says a program is running, so the line number is
  // still recoverable, and it is gone by the time the bridge sees it.
  if (charcount[OSERIAL] != 0) outch('\n');
  outsc("Break");
  if (st != SINT) {
    outsc(" in ");
    outnumber(myline(here));
  }
  outch('\n');

  return BREAKCHAR;
}

// BASIC's @A. Also what GET tests before calling inch().
uint16_t availch() { return (uint16_t)inputProgramKeyCount(); }

// Reads one line. This is where this firmware's own screen editor has to take
// over from the interpreter's: upstream's version drives its console directly,
// but here the "console" is a character grid with a cursor the user can move
// anywhere, wrapped logical lines, and its own Enter semantics. Left
// unimplemented until the UI is actually switched over; returning 0 means "no
// line available", which is safe.
uint16_t consins(char* b, uint16_t nb) {
  (void)b;
  (void)nb;
  return 0;
}

uint16_t ins(char* b, uint16_t nb) { return consins(b, nb); }

void ioinit() {
  bsystype = SYSTYPE_UNKNOWN;
  iodefaults();
}

void iodefaults() {
  od = odd;
  id = idd;
}

int iostat(int channel) {
  switch (channel) {
    case ISERIAL: return 1;   // console always present
    case IFILE:   return 1;   // SD card
    default:      return 0;
  }
}

void serialflush() {}
uint8_t serialstat(uint8_t c) { return c == 0 ? 1 : 0; }

// ---------------------------------------------------------------------------
// Timing and scheduling
// ---------------------------------------------------------------------------

// Called by the interpreter after every statement. Two jobs, both of which
// My-Basic needed a bolted-on stepped handler to achieve:
//
// 1. Yield. The interpreter runs inside loopTask, so without this a BASIC loop
//    starves the FreeRTOS idle task and trips the watchdog -- the exact freeze
//    hit once already (see docs/DEVELOPMENT_LOG.md).
// 2. Repaint. loop() can't redraw while a program is running, because the
//    program *is* what loop() is currently doing, so PRINT output would sit
//    invisible in the grid until the program ended.
//
// The repaint is throttled by wall-clock time rather than statement count: a
// tight loop must not try to redraw faster than the e-ink panel can refresh
// (~640ms), regardless of how many statements it gets through in between.
// Milliseconds of *running* between screen refreshes. An e-ink FAST_REFRESH
// costs roughly 700ms and blocks, so this is a duty cycle, not a frame rate:
// at 400 the program gets about a third of the time and output still appears
// in useful chunks.
static constexpr unsigned long FLUSH_INTERVAL_MS = 400;

// Statements between scheduler yields. One yield per statement (what this
// used to do) caps execution at the tick rate -- 1000Hz here, so ~1000
// statements/second no matter how trivial they are. The watchdog only needs
// feeding every few seconds, and 16 statements is microseconds of compute, so
// this costs nothing in responsiveness and lifts the ceiling considerably.
static constexpr unsigned int YIELD_EVERY = 16;

void byield() {
  static unsigned int sinceYield = 0;
  if (++sinceYield >= YIELD_EVERY) {
    sinceYield = 0;
    vTaskDelay(1);
  }

  if (!termDirty) return;

  static unsigned long lastFlush = 0;
  if (millis() - lastFlush < FLUSH_INTERVAL_MS) return;

  termDirty = false;
  screenEditorFlushDisplay();
  // Timed from when the refresh *finished*, not when it started. Marking it
  // beforehand measured start-to-start, and since a refresh takes longer than
  // the interval, every byield() after one immediately qualified for the next
  // -- refreshing back to back with almost no execution in between, which is
  // what made a print/goto loop take seconds per line.
  lastFlush = millis();
}

void bdelay(uint32_t t) {
  if (t == 0) return;
  vTaskDelay(t / portTICK_PERIOD_MS);
}

void breakpinbegin() {}

void restartsystem() { ESP.restart(); }

long freeRam() { return (long)ESP.getFreeHeap(); }
long freememorysize() { return freeRam(); }

// ---------------------------------------------------------------------------
// Filesystem -> SD card, under the same /MicroBASIC/programs used by SAVE/LOAD
// ---------------------------------------------------------------------------

static const char* TB_DIR = "/MicroBASIC/programs";


// Builds a path inside the programs directory, applying the same rules as
// SAVE/LOAD: name as typed, lower-cased, no separators. Keeping this identical
// matters -- a program saved from BASIC's own SAVE must be the same file the
// VC picker and the web file manager see.
static void tbPath(const char* name, char* out, int outSize) {
  char safe[MAX_FILENAME_LEN];
  int n = 0;
  for (const char* p = name; *p && n < (int)sizeof(safe) - 1; p++) {
    char c = *p;
    if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' ||
        c == '>' || c == '|') {
      c = '_';
    } else if (c >= 'A' && c <= 'Z') {
      c = (char)(c - 'A' + 'a');
    }
    safe[n++] = c;
  }
  safe[n] = '\0';
  snprintf(out, outSize, "%s/%s", TB_DIR, safe);
}

static void ensureDir() {
  if (!SdMan.exists("/MicroBASIC")) SdMan.mkdir("/MicroBASIC");
  if (!SdMan.exists(TB_DIR)) SdMan.mkdir(TB_DIR);
}

void fsbegin() { ensureDir(); }

uint8_t ifileopen(const char* filename) {
  char path[160];
  tbPath(filename, path, sizeof(path));
  if (ifile) ifile.close();
  ifile = SdMan.open(path, O_RDONLY);
  return ifile ? 1 : 0;
}

void ifileclose() {
  if (ifile) ifile.close();
}

uint8_t ofileopen(const char* filename, const char* m) {
  ensureDir();
  char path[160];
  tbPath(filename, path, sizeof(path));
  if (ofile) ofile.close();
  const bool append = (m && m[0] == 'a');
  ofile = SdMan.open(path, append ? (O_WRONLY | O_CREAT | O_APPEND) : (O_WRONLY | O_CREAT | O_TRUNC));
  return ofile ? 1 : 0;
}

void ofileclose() {
  if (ofile) ofile.close();
}

char fileread() {
  if (!ifile) return 0;
  const int c = ifile.read();
  return c < 0 ? 0 : (char)c;
}

int fileavailable() { return ifile ? ifile.available() : 0; }

void removefile(const char* filename) {
  char path[160];
  tbPath(filename, path, sizeof(path));
  SdMan.remove(path);
}

void rootopen() {
  if (rootDir) rootDir.close();
  rootDir = SdMan.open(TB_DIR);
}

uint8_t rootnextfile() {
  if (!rootDir) return 0;
  if (rootEntry) rootEntry.close();
  rootEntry = rootDir.openNextFile();
  if (!rootEntry) return 0;
  rootEntry.getName(rootNameBuf, sizeof(rootNameBuf));
  return 1;
}

const char* rootfilename() { return rootNameBuf; }
uint32_t rootfilesize() { return rootEntry ? (uint32_t)rootEntry.size() : 0; }
uint8_t rootisfile() { return (rootEntry && !rootEntry.isDirectory()) ? 1 : 0; }

void rootfileclose() {
  if (rootEntry) rootEntry.close();
}

void rootclose() {
  if (rootEntry) rootEntry.close();
  if (rootDir) rootDir.close();
}

// ---------------------------------------------------------------------------
// Stubs: peripherals this device doesn't have or doesn't expose to BASIC.
// These exist only because the interpreter references them from tables that
// compile in regardless of the feature flags; none of them are reachable with
// the language set configured in patches/tinybasic/01_configure_language.py.
// ---------------------------------------------------------------------------

// GPIO / analog
void pinm(uint8_t, uint8_t) {}
int pinread(uint8_t) { return 0; }
uint8_t dread(uint8_t) { return 0; }
void dwrite(uint8_t, uint8_t) {}
uint16_t aread(uint8_t) { return 0; }
void awrite(uint8_t, uint16_t) {}
int ddrread(uint8_t) { return 0; }
void ddrwrite(uint8_t, int) {}
int portread(uint8_t) { return 0; }
void portwrite(uint8_t, int) {}

// Display / VT52 -- the terminal here is the firmware's own, not upstream's
uint8_t dspactive() { return 0; }
char dspwaitonscroll() { return 0; }

// EEPROM. Could be mapped onto NVS later if a BASIC program ever wants
// persistent storage beyond files; reported as zero-length so the interpreter
// treats it as absent rather than as a broken device.
uint16_t elength() { return 0; }
int8_t eread(uint16_t) { return 0; }
void eupdate(uint16_t, int8_t) {}
void eflush() {}

// Filesystem odds and ends
uint8_t fsstat(uint8_t) { return 1; }
void formatdisk(uint8_t) {}
int cheof(int) { return 0; }

// Printer channel
char prtopen(char*, uint16_t) { return 0; }
void prtclose() {}
uint8_t prtstat(uint8_t) { return 0; }
void prtset(uint32_t) {}

// Networking
uint8_t netconnected() { return 0; }
uint8_t mqttstate() { return 0; }

// Real-time clock
void timeinit() {}
uint16_t rtcget(uint8_t) { return 0; }
void rtcset(uint8_t, uint16_t) {}

// Interrupt-driven profiling hooks
void fastticker() {}
int avgfastticker() { return 0; }
void clearfasttickerprofile() {}
