#pragma once

#include <cstdint>

// MicroBASIC's SCREEN 0/1/2/3 terminal: a scrolling character-cell
// display (like a classic 1980s BASIC's screen) plus cursor/logical-line
// tracking for the "type a numbered line, it's program text; type
// anything else, it runs immediately" editing model. See the MicroBASIC
// repo's docs/DEVELOPMENT_LOG.md for the full spec and the My-Basic
// integration this feeds into.
//
// Scrolling: typing or printing past the last row shifts every row up by
// one and clears the new bottom row -- whatever scrolled off is gone
// (this is a terminal, not a persistent buffer). The actual PROGRAM lives
// in program_store.h, entirely separate from what's currently visible.

void screenEditorSetMode(int n);  // 0-3, see README's SCREEN table
int screenEditorGetMode();
int screenEditorCols();
int screenEditorRows();
int screenEditorCellW();
int screenEditorCellH();
int screenEditorMarginX();
int screenEditorFontId();

// Clears the terminal (not the program store) and resets cursor/logical-
// line tracking. Does NOT change SCREEN mode.
void screenEditorReset();

uint32_t screenEditorGetCell(int row, int col);
int screenEditorGetCursorRow();
int screenEditorGetCursorCol();

// Cursor navigation -- every one of these resets "logical line start" to
// wherever the cursor ends up (see screenEditorGetLogicalLineText): moving
// the cursor deliberately means whatever row you land on is treated as
// its own independent line from here, breaking any in-progress multi-row
// continuation.
void screenEditorMoveCursor(int dRow, int dCol);
void screenEditorGoHome();      // column 0 of the current row
void screenEditorSetCursor(int row, int col);  // absolute, clamped; LOCATE
void screenEditorGoEnd();       // just past the last non-blank column of the current row
void screenEditorGoFirstRow();  // PgUp
void screenEditorGoLastRow();   // PgDn

// Typing/backspacing wrap between rows *without* resetting logical-line
// start -- this is what lets one BASIC line span multiple physical rows
// (because it was long enough to wrap while typing) and still be read as
// ONE line when Enter is eventually pressed on the wrapped continuation.
// Scrolls the terminal when insertion wraps past the last row.
void screenEditorInsertCodepoint(uint32_t cp);
void screenEditorBackspace();

// Concatenates every row from the logical-line start through the cursor's
// row (ASCII only -- non-ASCII becomes '?'; command/line-number parsing
// never needs more than that), trailing spaces trimmed only on the last
// row. This is what actually gets parsed when Enter is pressed.
void screenEditorGetLogicalLineText(char* out, int outSize);

// Clears every row of the current logical line back to spaces and moves
// the cursor to column 0 of the first of those rows -- used for direct-
// mode commands (MENU/LIST/RUN/...), which shouldn't linger on screen
// the way an accepted numbered program line does.
void screenEditorClearLogicalLine();

// Moves to column 0 of the next row (scrolling if the cursor was on the
// last row) and makes that new row the logical-line start -- call after
// finishing whatever Enter triggered, to begin fresh input.
void screenEditorStartNewInputLine();

// How many rows remain from the cursor's row to the bottom of the
// screen, inclusive -- for the LIST/FILES pagination logic in
// input_handler.cpp to know when to stop and show "MORE?".
int screenEditorRowsLeftOnScreen();

// --- Terminal output (BASIC PRINT, error messages, MORE? etc.) ---
// Prints UTF-8 text at the cursor. Embedded '\n' moves to column 0 of the
// next row (scrolling as needed); otherwise wraps at the column boundary
// exactly like typing does. No word-wrap -- fixed character grid, same as
// the rest of this project.
void screenEditorTermPrint(const char* utf8Text);
void screenEditorTermPrintLine(const char* utf8Text);  // + trailing newline

// --- SAVE/LOAD: the actual program (program_store.h), as plain text ---
// Stored under /MicroBASIC/programs/, under exactly the name typed -- no
// extension is forced on, so SAVE "TESTE" produces a file called TESTE.
// LOAD tries the typed name first and falls back to name + ".bas", so
// round-tripping works either way and a .bas dropped in from a PC is still
// found by its bare name.
//
// The file content is always plain text (`10 PRINT "X"` per line, exactly
// what LIST shows), never a tokenised/binary form, so programs can be read
// and edited directly off the SD card without this firmware.
//
// These report *why* they failed rather than a bare bool: in a BASIC
// environment the difference between "no such file", "disk error" and
// "program too big to serialise" is exactly what the user needs to act on,
// and all three used to surface as the same "?Save failed".
enum class ProgramFileResult {
  OK,
  BAD_NAME,    // empty, or nothing usable left after sanitising
  NOT_FOUND,   // LOAD only
  TOO_LARGE,   // program doesn't fit PROGRAM_TEXT_BUFFER_SIZE, or file bigger than it
  IO_ERROR,    // SD read/write failed
  EMPTY,       // LOAD only: file read fine but held no valid numbered lines
};

// One-line message for the terminal, e.g. "?File not found". Never null.
const char* programFileResultMessage(ProgramFileResult r);

ProgramFileResult screenEditorSaveProgram(const char* name);
ProgramFileResult screenEditorLoadProgram(const char* name);  // replaces the program store

// Draws the terminal and blocks until the e-ink refresh finishes. Normal
// screen updates go through main.cpp's loop() (draw, then poll the
// refresh non-blockingly on later iterations) -- but a running BASIC
// program blocks loopTask for its whole duration inside mb_run(), so
// nothing printed by PRINT would ever actually reach the display until
// the program finished. The runtime's byield() calls this periodically (throttled
// by time, not by print volume) during RUN so a loop's output is visible
// as it goes, not just as a final frame when the program stops or is
// broken out of. Defined in main.cpp, which owns the renderer/gpio
// instances.
void screenEditorFlushDisplay();
