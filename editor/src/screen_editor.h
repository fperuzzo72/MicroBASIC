#pragma once

#include <cstdint>

// MicroBASIC's SCREEN 1 grid editor -- first test pass, no BASIC
// interpreter yet, just a fixed 48x15 character-cell text surface. See
// the MicroBASIC repo's docs/DEVELOPMENT_LOG.md for the SCREEN mode spec
// this implements (font, cell size, centering).
//
// No scrolling/paging in this first pass: the cursor simply stops
// advancing at the last cell rather than wrapping/scrolling the grid.

void screenEditorReset();

uint32_t screenEditorGetCell(int row, int col);
int screenEditorGetCursorRow();
int screenEditorGetCursorCol();

// dRow/dCol: -1/0/+1. Clamps at grid edges (no wraparound).
void screenEditorMoveCursor(int dRow, int dCol);

// Writes at the cursor and advances it (wraps to the start of the next
// row at the last column; stops at the last cell of the last row).
void screenEditorInsertCodepoint(uint32_t cp);

// Moves the cursor back one cell (wrapping to the end of the previous
// row) and clears that cell.
void screenEditorBackspace();

// Moves the cursor to column 0 of the next row (stops at the last row).
void screenEditorNewline();
