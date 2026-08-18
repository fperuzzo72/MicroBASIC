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
// STATUS: skeleton. The contract is complete and this compiles and links, but
// it is not yet wired into the UI -- nothing calls basicSetup()/basicLoop()
// yet, and mb_bridge.cpp (My-Basic) is still the interpreter behind the
// SCREEN_EDITOR. See docs/DEVELOPMENT_LOG.md for the migration plan.

#include "config.h"
#include "screen_editor.h"
#include "input_handler.h"

#include <Arduino.h>
#include <SDCardManager.h>

#include <cstdio>
#include <cstring>

// Include order matters and is not arbitrary: runtime.h refers to constants
// (BUFSIZE, SPIRAMSBSIZE, the device ids) that hardware.h defines, and
// common.h has to come first. This is the same order upstream's own
// runtime.c uses.
extern "C" {
#include "common.h"
#include "hardware.h"
#include "runtime.h"
}

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

// ---------------------------------------------------------------------------
// Console I/O -> the SCREEN 0-3 terminal
// ---------------------------------------------------------------------------

void outch(char c) {
  if (c == '\r') return;  // terminal treats '\n' alone as end of line
  if (c == '\n') {
    screenEditorStartNewInputLine();
    charcount[od > 0 && od < 5 ? od : 0] = 0;
    return;
  }
  const char s[2] = {c, '\0'};
  screenEditorTermPrint(s);
  if (od >= 0 && od < 5) charcount[od]++;
}

void outs(char* s, uint16_t l) {
  for (uint16_t i = 0; i < l; i++) outch(s[i]);
}

// The interpreter polls for input rather than blocking, so these hand it
// whatever the key queue has. Actual line editing (cursor keys, backspace,
// the wrapped-logical-line model) stays in screen_editor.cpp/input_handler.cpp
// -- consins() below is deliberately not upstream's own line editor.
char inch() { return 0; }
char checkch() { return 0; }
uint16_t availch() { return 0; }

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

// Yields to FreeRTOS. Not optional on this device: the interpreter runs inside
// loopTask, so without a yield here a BASIC loop starves the idle task and
// trips the watchdog -- exactly the freeze already hit once with My-Basic (see
// docs/DEVELOPMENT_LOG.md). The interpreter calls byield() regularly by
// design, which is why this integration doesn't need My-Basic's stepped-handler
// workaround.
void byield() { vTaskDelay(1); }

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

static FsFile ifile;      // open for reading
static FsFile ofile;      // open for writing
static FsFile rootDir;    // directory walk
static FsFile rootEntry;  // current entry in that walk
static char rootNameBuf[MAX_FILENAME_LEN];

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
