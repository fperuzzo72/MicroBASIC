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

// Clears one row back to spaces (used before executing a LOAD/SAVE/MENU
// command line, so the command text itself doesn't end up saved as if it
// were program content).
void screenEditorClearRow(int row);

// Copies the cursor's row into `out` as plain ASCII (non-ASCII codepoints
// become '?'), trimmed of trailing spaces, always null-terminated. Used
// for recognizing LOAD/SAVE/MENU command lines -- those keywords and
// filenames are ASCII-only, so this is only for command *recognition*,
// not general text export.
void screenEditorGetCurrentLineText(char* out, int outSize);

// Saves/loads the whole grid as plain UTF-8 text (one line per row,
// trailing spaces trimmed) under /MicroBASIC/programs/<name>.txt --
// `name` is sanitized (no '/', truncated) and gets a .txt extension if it
// doesn't already have one. Load resets the grid and cursor first;
// missing file / IO error returns false and leaves the grid untouched.
bool screenEditorSave(const char* name);
bool screenEditorLoad(const char* name);
