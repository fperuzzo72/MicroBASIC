#include "screen_editor.h"

#include "config.h"

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
