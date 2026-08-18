#pragma once

// The actual BASIC program: a sparse table of line-number -> text, sorted
// by ascending line number, exactly like a classic 1980s BASIC's program
// memory. This is what LIST/SAVE/RUN work from -- separate from whatever
// happens to currently be visible on the Screen Editor's scrolling
// terminal (see screen_editor.h), which is transient except for what's
// been committed here by pressing Enter on a numbered line.

// Sized to actually fit in the ESP32-C3's RAM alongside everything else --
// the first pass at these (200 * 200) overflowed DRAM by ~12KB once linked.
static constexpr int MAX_PROGRAM_LINES = 100;
static constexpr int MAX_PROGRAM_LINE_LEN = 160;

void programStoreClear();
int programStoreCount();

// Inserts or replaces the text for line `num`. Passing empty/whitespace-
// only text deletes the line instead, matching classic BASIC ("type the
// line number alone to remove it").
void programStoreSet(int num, const char* text);

// 0-indexed, in ascending line-number order. Returns false past the end.
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
