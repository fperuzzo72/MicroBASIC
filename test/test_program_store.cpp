// Host test for program_store's packed-buffer storage. Compiles the real
// program_store.cpp unmodified -- it only uses standard headers.
#include "program_store.h"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>

static int failures = 0;

static void check(bool cond, const char* what) {
  if (!cond) {
    printf("  FAIL: %s\n", what);
    failures++;
  }
}

// Dump the store as "num:text|num:text|..." for easy comparison.
static std::string dump() {
  std::string out;
  int num;
  const char* text;
  for (int i = 0; programStoreGetByIndex(i, &num, &text); i++) {
    if (!out.empty()) out += "|";
    out += std::to_string(num) + ":" + text;
  }
  return out;
}

static void expectDump(const char* expected, const char* what) {
  std::string got = dump();
  if (got != expected) {
    printf("  FAIL: %s\n    expected: %s\n    got:      %s\n", what, expected, got.c_str());
    failures++;
  }
}

int main() {
  printf("== insertion keeps ascending order regardless of entry order ==\n");
  programStoreClear();
  programStoreSet(30, "PRINT 30");
  programStoreSet(10, "PRINT 10");
  programStoreSet(20, "PRINT 20");
  expectDump("10:PRINT 10|20:PRINT 20|30:PRINT 30", "out-of-order insert sorts");
  check(programStoreCount() == 3, "count is 3");

  printf("== replacing an existing line, same and different lengths ==\n");
  programStoreSet(20, "PRINT \"much longer replacement text\"");
  expectDump("10:PRINT 10|20:PRINT \"much longer replacement text\"|30:PRINT 30", "grow in place");
  check(programStoreCount() == 3, "replace does not change count");
  programStoreSet(20, "X");
  expectDump("10:PRINT 10|20:X|30:PRINT 30", "shrink in place");

  printf("== blank text deletes (classic 'type the number alone') ==\n");
  programStoreSet(20, "");
  expectDump("10:PRINT 10|30:PRINT 30", "empty string deletes");
  check(programStoreCount() == 2, "count drops to 2");
  programStoreSet(10, "   ");
  expectDump("30:PRINT 30", "whitespace-only deletes");
  programStoreSet(999, "");
  expectDump("30:PRINT 30", "deleting a nonexistent line is a no-op");

  printf("== boundary line numbers ==\n");
  programStoreClear();
  programStoreSet(1, "FIRST");
  programStoreSet(65535, "LAST");
  programStoreSet(32768, "MID");
  expectDump("1:FIRST|32768:MID|65535:LAST", "uint16 range endpoints survive");

  printf("== random vs sequential GetByIndex agree ==\n");
  programStoreClear();
  for (int i = 1; i <= 50; i++) programStoreSet(i * 10, ("L" + std::to_string(i)).c_str());
  std::string sequential = dump();
  int num;
  const char* text;
  // Random access, backwards -- forces the sequential memo to reset.
  std::string backwards;
  for (int i = 49; i >= 0; i--) {
    check(programStoreGetByIndex(i, &num, &text), "random index in range");
    backwards = std::to_string(num) + ":" + text + (backwards.empty() ? "" : "|" + backwards);
  }
  check(backwards == sequential, "backwards random access matches sequential");
  check(!programStoreGetByIndex(50, &num, &text), "index past end returns false");
  check(!programStoreGetByIndex(-1, &num, &text), "negative index returns false");

  printf("== delete from the middle of a large program ==\n");
  programStoreSet(250, "");
  check(programStoreCount() == 49, "count drops after middle delete");
  check(programStoreGetByIndex(24, &num, &text) && num == 260, "records after the hole shift down");

  printf("== serialize / load round-trip ==\n");
  programStoreClear();
  programStoreSet(10, "PRINT \"HELLO\"");
  programStoreSet(20, "GOTO 10");
  std::string before = dump();
  static char buf[PROGRAM_BUFFER_SIZE];
  check(programStoreSerialize(buf, sizeof(buf)), "serialize succeeds");
  check(strcmp(buf, "10 PRINT \"HELLO\"\n20 GOTO 10\n") == 0, "serialised .bas format");
  programStoreClear();
  check(programStoreCount() == 0, "cleared");
  programStoreLoadSerialized(buf);
  check(dump() == before, "round-trip restores identical program");

  printf("== GOTO/GOSUB label rewriting still correct ==\n");
  programStoreClear();
  programStoreSet(10, "PRINT \"GOTO 99\"");  // inside a string: must NOT rewrite
  programStoreSet(20, "GOTO 10");
  programStoreSet(30, "gosub 10");  // lowercase
  check(programStoreBuildRunSource(buf, sizeof(buf)), "run source builds");
  check(strstr(buf, "L10:\n") != nullptr, "line label emitted");
  check(strstr(buf, "PRINT \"GOTO 99\"") != nullptr, "GOTO inside string literal untouched");
  check(strstr(buf, "GOTO L10") != nullptr, "bare GOTO rewritten to label");
  // The rewriter normalises the keyword to upper case while rewriting the
  // target. Harmless: this source is internal and never shown -- LIST prints
  // the stored text, which keeps the user's original casing.
  check(strstr(buf, "GOSUB L10") != nullptr, "lowercase gosub rewritten (and upper-cased)");
  printf("  --- run source ---\n%s  ------------------\n", buf);

  printf("== buffer-full behaviour ==\n");
  programStoreClear();
  char longLine[MAX_PROGRAM_LINE_LEN];
  memset(longLine, 'X', sizeof(longLine) - 1);
  longLine[sizeof(longLine) - 1] = '\0';
  int stored = 0;
  bool refused = false;
  for (int i = 1; i <= 500; i++) {
    if (programStoreSet(i, longLine)) {
      stored++;
    } else {
      refused = true;
      break;
    }
  }
  check(refused, "eventually refuses instead of overflowing");
  check(programStoreCount() == stored, "count matches successful stores only");
  check(programStoreBytesUsed() <= PROGRAM_BUFFER_SIZE, "never exceeds buffer size");
  printf("  (stored %d max-length lines, %zu bytes used, %zu free)\n", stored,
         programStoreBytesUsed(), programStoreBytesFree());
  // The store must still be fully walkable and correctly ordered after filling up.
  int prev = 0;
  int walked = 0;
  for (int i = 0; programStoreGetByIndex(i, &num, &text); i++) {
    check(num > prev, "still strictly ascending after fill");
    prev = num;
    walked++;
  }
  check(walked == stored, "walk visits exactly the stored lines");

  printf("== over-long line text is truncated, not overflowed ==\n");
  programStoreClear();
  char tooLong[MAX_PROGRAM_LINE_LEN * 2];
  memset(tooLong, 'Y', sizeof(tooLong) - 1);
  tooLong[sizeof(tooLong) - 1] = '\0';
  check(programStoreSet(10, tooLong), "over-long line accepted");
  check(programStoreGetByIndex(0, &num, &text), "and retrievable");
  check(strlen(text) == MAX_PROGRAM_LINE_LEN - 1, "truncated to the per-line cap");

  printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "ALL PASSED", failures,
         failures == 1 ? "" : "s");
  return failures ? 1 : 0;
}
