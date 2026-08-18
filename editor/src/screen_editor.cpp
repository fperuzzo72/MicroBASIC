#include "screen_editor.h"

#include "config.h"
#include "sd_backup.h"
#include <SDCardManager.h>
#include <Utf8.h>
#include <cstdio>
#include <cstring>

static uint32_t grid[SCREEN_EDITOR_ROWS][SCREEN_EDITOR_COLS];
static int cursorRow = 0;
static int cursorCol = 0;

void screenEditorReset() {
  for (int r = 0; r < SCREEN_EDITOR_ROWS; r++) {
    for (int c = 0; c < SCREEN_EDITOR_COLS; c++) {
      grid[r][c] = ' ';
    }
  }
  cursorRow = 0;
  cursorCol = 0;
}

uint32_t screenEditorGetCell(int row, int col) {
  if (row < 0 || row >= SCREEN_EDITOR_ROWS || col < 0 || col >= SCREEN_EDITOR_COLS) return ' ';
  return grid[row][col];
}

int screenEditorGetCursorRow() { return cursorRow; }
int screenEditorGetCursorCol() { return cursorCol; }

static void clampCursor() {
  if (cursorRow < 0) cursorRow = 0;
  if (cursorRow >= SCREEN_EDITOR_ROWS) cursorRow = SCREEN_EDITOR_ROWS - 1;
  if (cursorCol < 0) cursorCol = 0;
  if (cursorCol >= SCREEN_EDITOR_COLS) cursorCol = SCREEN_EDITOR_COLS - 1;
}

void screenEditorMoveCursor(int dRow, int dCol) {
  cursorRow += dRow;
  cursorCol += dCol;
  clampCursor();
}

void screenEditorInsertCodepoint(uint32_t cp) {
  grid[cursorRow][cursorCol] = cp;
  cursorCol++;
  if (cursorCol >= SCREEN_EDITOR_COLS) {
    cursorCol = 0;
    if (cursorRow < SCREEN_EDITOR_ROWS - 1) cursorRow++;
    else cursorCol = SCREEN_EDITOR_COLS - 1;  // last cell of last row: stop advancing
  }
}

void screenEditorBackspace() {
  if (cursorCol > 0) {
    cursorCol--;
  } else if (cursorRow > 0) {
    cursorRow--;
    cursorCol = SCREEN_EDITOR_COLS - 1;
  } else {
    return;  // already at the very first cell
  }
  grid[cursorRow][cursorCol] = ' ';
}

void screenEditorNewline() {
  cursorCol = 0;
  if (cursorRow < SCREEN_EDITOR_ROWS - 1) cursorRow++;
}

void screenEditorClearRow(int row) {
  if (row < 0 || row >= SCREEN_EDITOR_ROWS) return;
  for (int c = 0; c < SCREEN_EDITOR_COLS; c++) grid[row][c] = ' ';
}

void screenEditorGetCurrentLineText(char* out, int outSize) {
  int n = 0;
  int lastNonSpace = -1;
  for (int c = 0; c < SCREEN_EDITOR_COLS && n < outSize - 1; c++) {
    uint32_t cp = grid[cursorRow][c];
    char ch = (cp < 0x80) ? (char)cp : '?';
    out[n] = ch;
    if (ch != ' ') lastNonSpace = n;
    n++;
  }
  out[lastNonSpace + 1] = '\0';
}

// --- LOAD/SAVE ------------------------------------------------------------

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
  bool hasExt = strlen(safe) > 4 && strcmp(safe + strlen(safe) - 4, ".txt") == 0;
  snprintf(path, pathSize, "%s/%s%s", PROGRAMS_DIR, safe, hasExt ? "" : ".txt");
}

static void ensureProgramsDir() {
  if (!SdMan.exists("/MicroBASIC")) SdMan.mkdir("/MicroBASIC");
  if (!SdMan.exists(PROGRAMS_DIR)) SdMan.mkdir(PROGRAMS_DIR);
}

static void utf8Encode(uint32_t cp, char out[5]) {
  if (cp < 0x80) {
    out[0] = (char)cp;
    out[1] = '\0';
  } else if (cp < 0x800) {
    out[0] = (char)(0xC0 | (cp >> 6));
    out[1] = (char)(0x80 | (cp & 0x3F));
    out[2] = '\0';
  } else {
    out[0] = (char)(0xE0 | (cp >> 12));
    out[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
    out[2] = (char)(0x80 | (cp & 0x3F));
    out[3] = '\0';
  }
}

bool screenEditorSave(const char* name) {
  if (!name || !name[0]) return false;
  ensureProgramsDir();

  static char buf[SCREEN_EDITOR_ROWS * (SCREEN_EDITOR_COLS * 4 + 1) + 1];
  int pos = 0;
  for (int r = 0; r < SCREEN_EDITOR_ROWS; r++) {
    int lastNonSpace = -1;
    int rowStart = pos;
    for (int c = 0; c < SCREEN_EDITOR_COLS; c++) {
      char enc[5];
      utf8Encode(grid[r][c], enc);
      for (char* p = enc; *p && pos < (int)sizeof(buf) - 2; p++) buf[pos++] = *p;
      if (grid[r][c] != ' ') lastNonSpace = pos;
    }
    pos = (lastNonSpace >= 0) ? lastNonSpace : rowStart;  // trim trailing spaces
    if (pos < (int)sizeof(buf) - 2) buf[pos++] = '\n';
  }
  buf[pos] = '\0';

  char path[80];
  buildProgramPath(name, path, sizeof(path));
  bool ok = sdWriteFile(path, buf);
  DBG_PRINTF("[SCREEN] Save %s -> %s\n", path, ok ? "OK" : "FAILED");
  return ok;
}

bool screenEditorLoad(const char* name) {
  if (!name || !name[0]) return false;
  char path[80];
  buildProgramPath(name, path, sizeof(path));

  static char buf[SCREEN_EDITOR_ROWS * (SCREEN_EDITOR_COLS * 4 + 1) + 1];
  if (!sdReadFile(path, buf, sizeof(buf))) {
    DBG_PRINTF("[SCREEN] Load %s -> FAILED (not found)\n", path);
    return false;
  }

  screenEditorReset();
  const unsigned char* p = (const unsigned char*)buf;
  for (int r = 0; r < SCREEN_EDITOR_ROWS; r++) {
    int c = 0;
    uint32_t cp;
    while (c < SCREEN_EDITOR_COLS && (cp = utf8NextCodepoint(&p)) != 0 && cp != '\n') {
      grid[r][c++] = cp;
    }
    // If the line was longer than the grid width, skip the rest of it.
    if (cp != 0 && cp != '\n') {
      while ((cp = utf8NextCodepoint(&p)) != 0 && cp != '\n') {}
    }
    if (cp == 0) break;  // end of file
  }
  cursorRow = 0;
  cursorCol = 0;
  DBG_PRINTF("[SCREEN] Load %s -> OK\n", path);
  return true;
}
