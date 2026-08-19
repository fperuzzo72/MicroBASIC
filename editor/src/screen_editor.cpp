#include "screen_editor.h"

#include "config.h"
#include "program_store.h"
#include "sd_backup.h"
#include <SDCardManager.h>
#include <Utf8.h>
#include <cstdio>
#include <cstring>
#include <strings.h>  // strcasecmp

struct ScreenModeInfo {
  int cols, rows, cellW, cellH, marginX, fontId;
};

// (800 - cols*cellW)/2 margin, verified to land on exactly 16px for modes
// 0-2 and 0 for mode 3 -- see README's "Column math". Row math needs no
// margin in any mode (480 divides evenly by every cellH).
static const ScreenModeInfo MODES[4] = {
    {32, 10, 24, 48, 16, FONT_SCREEN_MONO_0},
    {48, 15, 16, 32, 16, FONT_SCREEN_MONO_1},
    {64, 20, 12, 24, 16, FONT_SCREEN_MONO_2},
    {80, 24, 10, 20, 0, FONT_SCREEN_MONO_3},
};

static int currentMode = 1;  // SCREEN 1 default

static uint32_t grid[SCREEN_EDITOR_MAX_ROWS][SCREEN_EDITOR_MAX_COLS];
static int cursorRow = 0;
static int cursorCol = 0;

// rowIsContinuation[r] == true means row r is the wrapped tail of row r-1,
// i.e. they're one logical line that ran past the right margin. Set only by
// the two places that can wrap (typing past the last column, and terminal
// output doing the same); cleared wherever a genuinely new line starts.
//
// The logical line's start and end are *derived* from these flags rather
// than tracked in a variable, which is both simpler and what fixes the real
// bug this replaced: the old code reset a `logicalLineStartRow` on every
// deliberate cursor move, so LISTing a line longer than the screen width
// and then arrowing onto its second row made Enter read only that tail.
// MSX walks the continuation chain in both directions from wherever the
// cursor happens to be, and reads the whole logical line regardless of
// where in it you pressed Enter -- which is exactly what makes "LIST, cursor
// up, edit in place, Enter" work on a long line. See docs/DEVELOPMENT_LOG.md.
static bool rowIsContinuation[SCREEN_EDITOR_MAX_ROWS];

int screenEditorGetMode() { return currentMode; }
int screenEditorCols() { return MODES[currentMode].cols; }
int screenEditorRows() { return MODES[currentMode].rows; }
int screenEditorCellW() { return MODES[currentMode].cellW; }
int screenEditorCellH() { return MODES[currentMode].cellH; }
int screenEditorMarginX() { return MODES[currentMode].marginX; }
int screenEditorFontId() { return MODES[currentMode].fontId; }

void screenEditorReset() {
  int cols = screenEditorCols();
  int rows = screenEditorRows();
  for (int r = 0; r < rows; r++) {
    for (int c = 0; c < cols; c++)
      grid[r][c] = ' ';
    rowIsContinuation[r] = false;
  }
  cursorRow = 0;
  cursorCol = 0;
}

// Walk up/down the continuation chain to find the full extent of the logical
// line the cursor is currently sitting anywhere within.
static int logicalLineStartRow() {
  int r = cursorRow;
  while (r > 0 && rowIsContinuation[r]) r--;
  return r;
}

static int logicalLineEndRow() {
  int r = cursorRow;
  int rows = screenEditorRows();
  while (r + 1 < rows && rowIsContinuation[r + 1]) r++;
  return r;
}

void screenEditorSetMode(int n) {
  if (n < 0) n = 0;
  if (n > 3) n = 3;
  currentMode = n;
  screenEditorReset();
}

uint32_t screenEditorGetCell(int row, int col) {
  if (row < 0 || row >= screenEditorRows() || col < 0 || col >= screenEditorCols()) return ' ';
  return grid[row][col];
}

int screenEditorGetCursorRow() { return cursorRow; }
int screenEditorGetCursorCol() { return cursorCol; }

static void clampCursor() {
  int cols = screenEditorCols();
  int rows = screenEditorRows();
  if (cursorRow < 0) cursorRow = 0;
  if (cursorRow >= rows) cursorRow = rows - 1;
  if (cursorCol < 0) cursorCol = 0;
  if (cursorCol >= cols) cursorCol = cols - 1;
}

// Navigation no longer has to maintain any logical-line state: the
// continuation flags travel with the rows themselves, so moving the cursor
// simply lands you somewhere within whatever logical line owns that row.
void screenEditorMoveCursor(int dRow, int dCol) {
  cursorRow += dRow;
  cursorCol += dCol;
  clampCursor();
}

void screenEditorGoHome() { cursorCol = 0; }

void screenEditorGoEnd() {
  int cols = screenEditorCols();
  int last = -1;
  for (int c = 0; c < cols; c++)
    if (grid[cursorRow][c] != (uint32_t)' ') last = c;
  cursorCol = (last < 0) ? 0 : ((last + 1 < cols) ? last + 1 : cols - 1);
}

void screenEditorGoFirstRow() {
  cursorRow = 0;
  clampCursor();
}

void screenEditorGoLastRow() {
  cursorRow = screenEditorRows() - 1;
  clampCursor();
}

static void scrollUp() {
  int cols = screenEditorCols();
  int rows = screenEditorRows();
  for (int r = 0; r < rows - 1; r++) {
    for (int c = 0; c < cols; c++)
      grid[r][c] = grid[r + 1][c];
    rowIsContinuation[r] = rowIsContinuation[r + 1];
  }
  for (int c = 0; c < cols; c++) grid[rows - 1][c] = ' ';
  rowIsContinuation[rows - 1] = false;
  // Row 0 can't be a continuation of anything once whatever preceded it has
  // scrolled off -- an extremely long wrapped line loses its true head here,
  // same best-effort limit the old code had.
  rowIsContinuation[0] = false;
}

// Advances the cursor to the next row, marking it as a continuation of the
// row just left -- the shared tail of "typed past the last column" and
// "printed past the last column", which are the only two ways a logical line
// legitimately spans rows.
static void wrapToNextRow() {
  int rows = screenEditorRows();
  cursorCol = 0;
  if (cursorRow < rows - 1) {
    cursorRow++;
    rowIsContinuation[cursorRow] = true;
  } else {
    scrollUp();  // cursorRow stays at rows-1
    rowIsContinuation[cursorRow] = true;
  }
}

void screenEditorInsertCodepoint(uint32_t cp) {
  int cols = screenEditorCols();
  grid[cursorRow][cursorCol] = cp;
  cursorCol++;
  if (cursorCol >= cols) wrapToNextRow();
}

void screenEditorBackspace() {
  int cols = screenEditorCols();
  if (cursorCol > 0) {
    cursorCol--;
  } else if (cursorRow > 0 && rowIsContinuation[cursorRow]) {
    // Only cross the row boundary when this row is the wrapped tail of the one
    // above, i.e. they are one logical line and the character before the
    // cursor really is up there. On a line the user started fresh, backspace
    // at column 0 must do nothing -- otherwise it walks back into, and eats,
    // whatever unrelated text happens to be on the previous row.
    rowIsContinuation[cursorRow] = false;
    cursorRow--;
    cursorCol = cols - 1;
  } else {
    return;
  }
  grid[cursorRow][cursorCol] = ' ';
}

// Reads the *entire* logical line the cursor is within -- from the top of
// its continuation chain to the bottom -- not just up to the cursor. That's
// the MSX rule: where you happen to be within the line when you press Enter
// doesn't change what gets read.
void screenEditorGetLogicalLineText(char* out, int outSize) {
  int cols = screenEditorCols();
  int start = logicalLineStartRow();
  int end = logicalLineEndRow();
  int n = 0;
  int lastNonSpace = -1;
  for (int r = start; r <= end && n < outSize - 1; r++) {
    for (int c = 0; c < cols && n < outSize - 1; c++) {
      uint32_t cp = grid[r][c];
      char ch = (cp < 0x80) ? (char)cp : '?';
      out[n] = ch;
      if (ch != ' ') lastNonSpace = n;
      n++;
    }
  }
  out[lastNonSpace + 1] = '\0';
}

void screenEditorClearLogicalLine() {
  int cols = screenEditorCols();
  int start = logicalLineStartRow();
  int end = logicalLineEndRow();
  for (int r = start; r <= end; r++) {
    for (int c = 0; c < cols; c++) grid[r][c] = ' ';
    rowIsContinuation[r] = false;
  }
  cursorRow = start;
  cursorCol = 0;
}

void screenEditorStartNewInputLine() {
  int rows = screenEditorRows();
  cursorCol = 0;
  if (cursorRow < rows - 1) {
    cursorRow++;
  } else {
    scrollUp();
  }
  rowIsContinuation[cursorRow] = false;  // a genuinely new line, not a wrap
}

int screenEditorRowsLeftOnScreen() {
  return screenEditorRows() - cursorRow;
}

void screenEditorTermPrint(const char* utf8Text) {
  const unsigned char* p = (const unsigned char*)utf8Text;
  uint32_t cp;
  while ((cp = utf8NextCodepoint(&p)) != 0) {
    if (cp == '\n') {
      screenEditorStartNewInputLine();
    } else {
      screenEditorInsertCodepoint(cp);
    }
  }
}

void screenEditorTermPrintLine(const char* utf8Text) {
  screenEditorTermPrint(utf8Text);
  screenEditorTermPrint("\n");
}

// --- SAVE/LOAD -------------------------------------------------------------

static const char* PROGRAMS_DIR = "/MicroBASIC/programs";
static const char* PROGRAM_EXT = ".bas";

// Big enough for the worst case by construction rather than by guess:
// directory + '/' + a full-length sanitised name + extension + NUL. The
// previous fixed 80 was 9 bytes short of that, so a long name silently
// produced a truncated (wrong) path instead of the file you asked for.
static constexpr int PROGRAM_PATH_MAX =
    20 /*strlen(PROGRAMS_DIR)*/ + 1 + MAX_FILENAME_LEN + 4 /*.bas*/ + 1;

const char* programFileResultMessage(ProgramFileResult r) {
  switch (r) {
    case ProgramFileResult::OK:        return "Ok";
    case ProgramFileResult::BAD_NAME:  return "?Bad file name";
    case ProgramFileResult::NOT_FOUND: return "?File not found";
    case ProgramFileResult::TOO_LARGE: return "?Program too large";
    case ProgramFileResult::IO_ERROR:  return "?Disk error";
    case ProgramFileResult::EMPTY:     return "?No program lines in file";
  }
  return "?Error";
}

// Maps a typed name onto something FAT can actually hold. Path separators
// and the characters FAT/SdFat reject become '_', leading/trailing spaces go
// (they're invisible in a listing and make files unopenable by the name
// shown), and everything else passes through -- including accents, which
// long filenames support and which a Portuguese-speaking user will type.
//
// Names are also folded to lower case, so the SD card ends up with one
// consistent casing rather than TESTE/Teste/teste as three different files,
// and SAVE/LOAD become case-insensitive to each other for free. Only ASCII
// A-Z is folded: tolower() on UTF-8 continuation bytes would corrupt
// accented characters.
static void sanitizeFilename(const char* name, char* out, int outSize) {
  while (*name == ' ') name++;
  int n = 0;
  for (const char* p = name; *p && n < outSize - 1; p++) {
    char c = *p;
    const bool illegal = (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' ||
                          c == '"' || c == '<' || c == '>' || c == '|');
    if (illegal) c = '_';
    else if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
    out[n++] = c;
  }
  while (n > 0 && out[n - 1] == ' ') n--;  // trailing spaces
  out[n] = '\0';
}

// Case-insensitive search of the programs directory, for files that arrived
// from a PC under some other casing (JOGO.BAS) than the lower-case one this
// firmware writes. Returns true and fills `out` with the real on-disk name.
static bool findIgnoringCase(const char* wanted, char* out, int outSize) {
  auto dir = SdMan.open(PROGRAMS_DIR);
  if (!dir || !dir.isDirectory()) {
    if (dir) dir.close();
    return false;
  }
  bool found = false;
  char name[256];
  dir.rewindDirectory();
  for (auto file = dir.openNextFile(); file; file = dir.openNextFile()) {
    file.getName(name, sizeof(name));
    if (name[0] != '.' && !file.isDirectory() && strcasecmp(name, wanted) == 0) {
      strncpy(out, name, outSize - 1);
      out[outSize - 1] = '\0';
      found = true;
    }
    file.close();
    if (found) break;
  }
  dir.close();
  return found;
}

// Builds the path for exactly the name as typed -- no extension is forced on.
// SAVE "TESTE" makes a file called TESTE, not TESTE.bas: the name you type is
// the name you get. LOAD compensates by also trying the .bas form (see
// screenEditorLoadProgram), so `SAVE "X"` / `LOAD "X"` round-trips and
// `LOAD "X"` still finds an X.bas that came from somewhere else.
// Returns false if nothing usable is left of the name after sanitising.
static bool buildProgramPath(const char* name, const char* suffix, char* path, int pathSize) {
  if (!name) return false;
  char safe[MAX_FILENAME_LEN];
  sanitizeFilename(name, safe, sizeof(safe));
  if (!safe[0]) return false;
  snprintf(path, pathSize, "%s/%s%s", PROGRAMS_DIR, safe, suffix);
  return true;
}

static void ensureProgramsDir() {
  if (!SdMan.exists("/MicroBASIC")) SdMan.mkdir("/MicroBASIC");
  if (!SdMan.exists(PROGRAMS_DIR)) SdMan.mkdir(PROGRAMS_DIR);
}

ProgramFileResult screenEditorSaveProgram(const char* name) {
  char path[PROGRAM_PATH_MAX];
  if (!buildProgramPath(name, "", path, sizeof(path))) return ProgramFileResult::BAD_NAME;

  // Content is always plain text -- "10 PRINT ..." one line per line, exactly
  // as LIST shows it -- so a saved program can be read (and edited) straight
  // off the SD card on a PC without this firmware to decode it. Only the
  // *name* is left alone; the format is deliberately not a binary/tokenised
  // one for that reason.
  static char buf[PROGRAM_TEXT_BUFFER_SIZE];
  if (!programStoreSerialize(buf, sizeof(buf))) {
    DBG_PRINTLN("[MB] Save failed: program text too large for buffer");
    return ProgramFileResult::TOO_LARGE;
  }

  ensureProgramsDir();
  const bool ok = sdWriteFile(path, buf);
  DBG_PRINTF("[MB] Save %s -> %s\n", path, ok ? "OK" : "FAILED");
  return ok ? ProgramFileResult::OK : ProgramFileResult::IO_ERROR;
}

ProgramFileResult screenEditorLoadProgram(const char* name) {
  char path[PROGRAM_PATH_MAX];
  if (!buildProgramPath(name, "", path, sizeof(path))) return ProgramFileResult::BAD_NAME;

  // Lookup order: the (lower-cased) name as typed, then that name + ".bas",
  // then a case-insensitive directory scan of both for files that came from
  // a PC under different casing. So `LOAD "Jogo"` finds jogo, jogo.bas,
  // JOGO or JOGO.BAS -- whichever is actually there.
  if (!SdMan.exists(path)) {
    char withExt[PROGRAM_PATH_MAX];
    const bool haveExt = buildProgramPath(name, PROGRAM_EXT, withExt, sizeof(withExt));
    if (haveExt && SdMan.exists(withExt)) {
      memcpy(path, withExt, sizeof(path));
    } else {
      char sanitized[MAX_FILENAME_LEN];
      char actual[MAX_FILENAME_LEN];
      sanitizeFilename(name, sanitized, sizeof(sanitized));
      char sanitizedExt[MAX_FILENAME_LEN];
      snprintf(sanitizedExt, sizeof(sanitizedExt), "%s%s", sanitized, PROGRAM_EXT);
      if (findIgnoringCase(sanitized, actual, sizeof(actual)) ||
          findIgnoringCase(sanitizedExt, actual, sizeof(actual))) {
        snprintf(path, sizeof(path), "%s/%s", PROGRAMS_DIR, actual);
      } else {
        DBG_PRINTF("[MB] Load %s -> NOT FOUND\n", path);
        return ProgramFileResult::NOT_FOUND;
      }
    }
  }

  static char buf[PROGRAM_TEXT_BUFFER_SIZE];
  if (!sdReadFile(path, buf, sizeof(buf))) {
    // Distinguishable from NOT_FOUND now that existence was checked first:
    // a file that exists but won't read is either a disk problem or bigger
    // than the buffer (sdReadFile reads at most bufSize-1 and can't tell us
    // which), and either way "not found" was the wrong thing to report.
    DBG_PRINTF("[MB] Load %s -> READ FAILED\n", path);
    return ProgramFileResult::IO_ERROR;
  }

  programStoreLoadSerialized(buf);
  if (programStoreCount() == 0) {
    // Read fine but yielded nothing: an empty file, or a text file that
    // isn't a BASIC program. Silently ending up with an empty program and an
    // "Ok" was actively misleading -- it looked like the load had worked.
    DBG_PRINTF("[MB] Load %s -> no valid lines\n", path);
    return ProgramFileResult::EMPTY;
  }

  DBG_PRINTF("[MB] Load %s -> OK (%d lines)\n", path, programStoreCount());
  return ProgramFileResult::OK;
}
