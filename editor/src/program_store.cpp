#include "program_store.h"

#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <strings.h>

// See program_store.h for the record layout. Header is two uint16s, accessed
// through memcpy rather than a packed struct so this stays alignment-safe.
static constexpr size_t REC_HEADER = 4;

static uint8_t buffer[PROGRAM_BUFFER_SIZE];
static size_t used = 0;
static int lineCount = 0;

// Sequential-access memo: LIST walks indices 0,1,2,... so remembering where
// the last lookup landed turns the whole listing from O(n^2) into O(n).
static int cursorIndex = 0;
static size_t cursorOffset = 0;

static inline uint16_t recLen(size_t off) {
  uint16_t v;
  memcpy(&v, buffer + off, sizeof(v));
  return v;
}

static inline uint16_t recNum(size_t off) {
  uint16_t v;
  memcpy(&v, buffer + off + 2, sizeof(v));
  return v;
}

static inline const char* recText(size_t off) {
  return reinterpret_cast<const char*>(buffer + off + REC_HEADER);
}

static inline void resetCursor() {
  cursorIndex = 0;
  cursorOffset = 0;
}

void programStoreClear() {
  used = 0;
  lineCount = 0;
  resetCursor();
}

int programStoreCount() { return lineCount; }
size_t programStoreBytesUsed() { return used; }
size_t programStoreBytesFree() { return PROGRAM_BUFFER_SIZE - used; }

// Offset of the record with exactly this line number, or `used` if absent.
static size_t findExact(int num) {
  size_t off = 0;
  while (off < used) {
    uint16_t n = recNum(off);
    if (n == num) return off;
    if (n > num) break;
    off += recLen(off);
  }
  return used;
}

// Offset where a record with this line number belongs (first record with a
// higher number, or the end).
static size_t findInsertPos(int num) {
  size_t off = 0;
  while (off < used && recNum(off) < num) off += recLen(off);
  return off;
}

static bool isBlank(const char* s) {
  for (; *s; s++) {
    if (!isspace((unsigned char)*s)) return false;
  }
  return true;
}

static void removeAt(size_t off) {
  const uint16_t len = recLen(off);
  memmove(buffer + off, buffer + off + len, used - off - len);
  used -= len;
  lineCount--;
  resetCursor();
}

bool programStoreSet(int num, const char* text) {
  const size_t existing = findExact(num);

  if (isBlank(text)) {
    if (existing < used) removeAt(existing);
    return true;
  }

  size_t textLen = strlen(text);
  if (textLen > (size_t)(MAX_PROGRAM_LINE_LEN - 1)) textLen = MAX_PROGRAM_LINE_LEN - 1;
  const size_t needed = REC_HEADER + textLen + 1;

  // Replacing: drop the old record first so the fit check below sees the
  // space it frees. Nothing else can fail after this point.
  if (existing < used) removeAt(existing);
  if (used + needed > PROGRAM_BUFFER_SIZE) return false;

  const size_t pos = findInsertPos(num);
  memmove(buffer + pos + needed, buffer + pos, used - pos);

  const uint16_t lenField = (uint16_t)needed;
  const uint16_t numField = (uint16_t)num;
  memcpy(buffer + pos, &lenField, sizeof(lenField));
  memcpy(buffer + pos + 2, &numField, sizeof(numField));
  memcpy(buffer + pos + REC_HEADER, text, textLen);
  buffer[pos + REC_HEADER + textLen] = '\0';

  used += needed;
  lineCount++;
  resetCursor();
  return true;
}

bool programStoreGetByIndex(int index, int* outNum, const char** outText) {
  if (index < 0 || index >= lineCount) return false;

  // Resume from the memo when moving forward from it, else start over.
  int i = 0;
  size_t off = 0;
  if (index >= cursorIndex && cursorOffset < used) {
    i = cursorIndex;
    off = cursorOffset;
  }
  while (i < index && off < used) {
    off += recLen(off);
    i++;
  }
  if (off >= used) return false;

  cursorIndex = index;
  cursorOffset = off;
  *outNum = recNum(off);
  *outText = recText(off);
  return true;
}

// Rewrites "GOTO n" / "GOSUB n" (case-insensitive, whole word, outside
// string literals) to "GOTO Ln" / "GOSUB Ln" -- see program_store.h for
// why (My-Basic's GOTO/GOSUB targets are alphabetic labels, verified
// against the real interpreter; bare numeric labels don't parse).
static void rewriteGotoGosub(const char* in, char* out, int outSize) {
  int n = 0;
  bool inString = false;
  const char* p = in;

  auto emit = [&](char c) {
    if (n < outSize - 1) out[n++] = c;
  };
  auto emitStr = [&](const char* s) {
    for (; *s; s++) emit(*s);
  };
  auto isWordChar = [](char c) { return isalnum((unsigned char)c) || c == '_'; };

  while (*p) {
    if (*p == '"') {
      inString = !inString;
      emit(*p);
      p++;
      continue;
    }
    if (!inString) {
      const char* kw = nullptr;
      size_t kwlen = 0;
      if (strncasecmp(p, "GOTO", 4) == 0 && (p == in || !isWordChar(p[-1])) && !isWordChar(p[4])) {
        kw = "GOTO";
        kwlen = 4;
      } else if (strncasecmp(p, "GOSUB", 5) == 0 && (p == in || !isWordChar(p[-1])) && !isWordChar(p[5])) {
        kw = "GOSUB";
        kwlen = 5;
      }
      if (kw) {
        emitStr(kw);
        p += kwlen;
        while (*p == ' ' || *p == '\t') {
          emit(*p);
          p++;
        }
        if (isdigit((unsigned char)*p)) {
          emit('L');
          while (isdigit((unsigned char)*p)) {
            emit(*p);
            p++;
          }
        }
        continue;
      }
    }
    emit(*p);
    p++;
  }
  out[n] = '\0';
}

bool programStoreBuildRunSource(char* out, int outSize) {
  int pos = 0;
  char rewritten[MAX_PROGRAM_LINE_LEN * 2];

  for (size_t off = 0; off < used; off += recLen(off)) {
    int n = snprintf(out + pos, outSize - pos, "L%d:\n", (int)recNum(off));
    if (n < 0 || pos + n >= outSize) return false;
    pos += n;

    rewriteGotoGosub(recText(off), rewritten, sizeof(rewritten));
    int n2 = snprintf(out + pos, outSize - pos, "%s\n", rewritten);
    if (n2 < 0 || pos + n2 >= outSize) return false;
    pos += n2;
  }
  out[pos] = '\0';
  return true;
}

bool programStoreSerialize(char* out, int outSize) {
  int pos = 0;
  for (size_t off = 0; off < used; off += recLen(off)) {
    int n = snprintf(out + pos, outSize - pos, "%d %s\n", (int)recNum(off), recText(off));
    if (n < 0 || pos + n >= outSize) return false;
    pos += n;
  }
  out[pos] = '\0';
  return true;
}

void programStoreLoadSerialized(const char* text) {
  programStoreClear();
  const char* p = text;
  while (*p) {
    const char* lineEnd = strchr(p, '\n');
    int len = lineEnd ? (int)(lineEnd - p) : (int)strlen(p);

    char buf[MAX_PROGRAM_LINE_LEN + 16];
    int copyLen = (len < (int)sizeof(buf) - 1) ? len : (int)sizeof(buf) - 1;
    memcpy(buf, p, copyLen);
    buf[copyLen] = '\0';

    char* endptr = nullptr;
    long num = strtol(buf, &endptr, 10);
    if (endptr != buf && num > 0 && num <= 65535) {
      const char* textStart = endptr;
      while (*textStart == ' ') textStart++;
      programStoreSet((int)num, textStart);
    }

    p = lineEnd ? lineEnd + 1 : p + len;
  }
}
