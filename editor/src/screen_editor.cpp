#include "screen_editor.h"

#include "config.h"
#include "program_store.h"
#include "sd_backup.h"
#include <SDCardManager.h>
#include <Utf8.h>
#include <cstdio>
#include <cstring>

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
static int logicalLineStartRow = 0;

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
  for (int r = 0; r < rows; r++)
    for (int c = 0; c < cols; c++)
      grid[r][c] = ' ';
  cursorRow = 0;
  cursorCol = 0;
  logicalLineStartRow = 0;
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

void screenEditorMoveCursor(int dRow, int dCol) {
  cursorRow += dRow;
  cursorCol += dCol;
  clampCursor();
  logicalLineStartRow = cursorRow;  // deliberate navigation always breaks continuation
}

void screenEditorGoHome() {
  cursorCol = 0;
  logicalLineStartRow = cursorRow;
}

void screenEditorGoEnd() {
  int cols = screenEditorCols();
  int last = -1;
  for (int c = 0; c < cols; c++)
    if (grid[cursorRow][c] != (uint32_t)' ') last = c;
  cursorCol = (last < 0) ? 0 : ((last + 1 < cols) ? last + 1 : cols - 1);
  logicalLineStartRow = cursorRow;
}

void screenEditorGoFirstRow() {
  cursorRow = 0;
  clampCursor();
  logicalLineStartRow = cursorRow;
}

void screenEditorGoLastRow() {
  cursorRow = screenEditorRows() - 1;
  clampCursor();
  logicalLineStartRow = cursorRow;
}

static void scrollUp() {
  int cols = screenEditorCols();
  int rows = screenEditorRows();
  for (int r = 0; r < rows - 1; r++)
    for (int c = 0; c < cols; c++)
      grid[r][c] = grid[r + 1][c];
  for (int c = 0; c < cols; c++) grid[rows - 1][c] = ' ';
  if (logicalLineStartRow > 0) logicalLineStartRow--;
  // else: the true start of the in-progress logical line already scrolled
  // off (an extremely long wrapped line) -- best effort, not reconstructible.
}

void screenEditorInsertCodepoint(uint32_t cp) {
  int cols = screenEditorCols();
  int rows = screenEditorRows();
  grid[cursorRow][cursorCol] = cp;
  cursorCol++;
  if (cursorCol >= cols) {
    cursorCol = 0;
    if (cursorRow < rows - 1) {
      cursorRow++;
    } else {
      scrollUp();  // cursorRow stays at rows-1
    }
  }
}

void screenEditorBackspace() {
  int cols = screenEditorCols();
  if (cursorCol > 0) {
    cursorCol--;
  } else if (cursorRow > 0) {
    cursorRow--;
    cursorCol = cols - 1;
  } else {
    return;  // already at the very first cell
  }
  grid[cursorRow][cursorCol] = ' ';
}

void screenEditorGetLogicalLineText(char* out, int outSize) {
  int cols = screenEditorCols();
  int n = 0;
  int lastNonSpace = -1;
  for (int r = logicalLineStartRow; r <= cursorRow && n < outSize - 1; r++) {
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
  for (int r = logicalLineStartRow; r <= cursorRow; r++)
    for (int c = 0; c < cols; c++) grid[r][c] = ' ';
  cursorRow = logicalLineStartRow;
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
  logicalLineStartRow = cursorRow;
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

static void sanitizeFilename(const char* name, char* out, int outSize) {
  int n = 0;
  for (const char* p = name; *p && n < outSize - 1; p++) {
    out[n++] = (*p == '/' || *p == '\\') ? '_' : *p;
  }
  out[n] = '\0';
}

static void buildProgramPath(const char* name, char* path, int pathSize) {
  char safe[MAX_FILENAME_LEN];
  sanitizeFilename(name, safe, sizeof(safe));
  bool hasExt = strlen(safe) > 4 && strcmp(safe + strlen(safe) - 4, ".bas") == 0;
  snprintf(path, pathSize, "%s/%s%s", PROGRAMS_DIR, safe, hasExt ? "" : ".bas");
}

static void ensureProgramsDir() {
  if (!SdMan.exists("/MicroBASIC")) SdMan.mkdir("/MicroBASIC");
  if (!SdMan.exists(PROGRAMS_DIR)) SdMan.mkdir(PROGRAMS_DIR);
}

bool screenEditorSaveProgram(const char* name) {
  if (!name || !name[0]) return false;
  ensureProgramsDir();

  static char buf[PROGRAM_TEXT_BUFFER_SIZE];
  if (!programStoreSerialize(buf, sizeof(buf))) {
    DBG_PRINTLN("[MB] Save failed: program text too large for buffer");
    return false;
  }

  char path[80];
  buildProgramPath(name, path, sizeof(path));
  bool ok = sdWriteFile(path, buf);
  DBG_PRINTF("[MB] Save %s -> %s\n", path, ok ? "OK" : "FAILED");
  return ok;
}

bool screenEditorLoadProgram(const char* name) {
  if (!name || !name[0]) return false;
  char path[80];
  buildProgramPath(name, path, sizeof(path));

  static char buf[PROGRAM_TEXT_BUFFER_SIZE];
  if (!sdReadFile(path, buf, sizeof(buf))) {
    DBG_PRINTF("[MB] Load %s -> FAILED (not found)\n", path);
    return false;
  }

  programStoreLoadSerialized(buf);
  DBG_PRINTF("[MB] Load %s -> OK (%d lines)\n", path, programStoreCount());
  return true;
}
