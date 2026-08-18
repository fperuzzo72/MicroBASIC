#pragma once

#include <cstddef>

// The actual BASIC program: line-number -> text, ordered by ascending line
// number, exactly like a classic 1980s BASIC's program memory. This is what
// LIST/SAVE/RUN work from -- separate from whatever happens to currently be
// visible on the Screen Editor's scrolling terminal (see screen_editor.h),
// which is transient except for what's been committed here by pressing
// Enter on a numbered line.
//
// Storage is one contiguous byte buffer of variable-length records, the way
// real 8-bit BASICs did it, rather than a fixed array of fixed-width slots:
//
//   [uint16 recLen][uint16 lineNumber][text bytes...][NUL]
//
// recLen covers the whole record including both header fields and the NUL,
// so walking the program is `off += recLen` and the end is `off == used`.
// Records are kept sorted by line number, so insert/delete is a memmove.
//
// Why this shape rather than the `ProgramLine lines[60]` array it replaced:
// that array cost 60 * (4 + 160) = ~9.8KB of *always-resident* static RAM no
// matter how small the program was, while simultaneously imposing two
// arbitrary ceilings (60 lines, 160 chars each) that a real BASIC wouldn't
// have. Packed records spend only what the program actually uses, drop the
// line-count ceiling entirely, and free static RAM back to the heap -- which
// on this device is not an abstract win: an earlier version of this file
// starved the BLE stack badly enough that the keyboard could not connect at
// all (see docs/DEVELOPMENT_LOG.md).

// Total program memory. A typical BASIC line runs ~20-30 bytes packed, so
// this holds roughly 270-400 lines -- against the 60-line ceiling of the
// fixed array it replaced, in *less* static RAM than that array's 9840
// bytes. Deliberately sized to keep the heap trend going the right way
// rather than to maximise capacity: the BLE stack needs a 20KB contiguous
// allocation at connect time and we have already been on the wrong side of
// that once. Tune here; nothing else depends on the value.
static constexpr size_t PROGRAM_BUFFER_SIZE = 8192;

// Per-line ceiling. Callers size their own stack buffers from this, so it
// stays part of the API even though storage itself is now variable-length.
static constexpr int MAX_PROGRAM_LINE_LEN = 160;

void programStoreClear();
int programStoreCount();

// Bytes used / still free, for a future FRE()-style report and for callers
// that want to warn before a program stops fitting.
size_t programStoreBytesUsed();
size_t programStoreBytesFree();

// Inserts or replaces the text for line `num`. Passing empty/whitespace-
// only text deletes the line instead, matching classic BASIC ("type the
// line number alone to remove it"). Returns false only if the program
// buffer is full and the line therefore could not be stored.
bool programStoreSet(int num, const char* text);

// 0-indexed, in ascending line-number order. Returns false past the end.
// Sequential access (index 0, 1, 2, ... as LIST does) is O(1) amortised via
// an internal cursor; random access walks from the start.
bool programStoreGetByIndex(int index, int* outNum, const char** outText);

// My-Basic uses alphabetic labels for GOTO/GOSUB targets, not line
// numbers (verified against the real interpreter -- a bare numeric label
// like "10:" is a parse error, "L10:" works). This builds the run source
// by prefixing every stored line with its own "LN:" label and rewriting
// any "GOTO n" / "GOSUB n" found in the text (outside string literals) to
// "GOTO Ln" / "GOSUB Ln", so classic `GOTO 10` keeps working as typed.
// Returns false if the result doesn't fit in outSize.
bool programStoreBuildRunSource(char* out, int outSize);

// Plain "N text\n" per line, ascending order -- the .bas save format.
// Returns false if it doesn't fit in outSize.
bool programStoreSerialize(char* out, int outSize);

// Parses the .bas format above back in, replacing the whole store.
// Malformed lines (no leading number) are skipped.
void programStoreLoadSerialized(const char* text);
