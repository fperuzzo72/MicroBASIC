#include "program_store.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <strings.h>

struct ProgramLine {
  int number;
  char text[MAX_PROGRAM_LINE_LEN];
};

static ProgramLine lines[MAX_PROGRAM_LINES];
static int lineCount = 0;

void programStoreClear() { lineCount = 0; }
int programStoreCount() { return lineCount; }

static int findExactIndex(int num) {
  for (int i = 0; i < lineCount; i++) {
    if (lines[i].number == num) return i;
    if (lines[i].number > num) break;
  }
  return -1;
}

static int findInsertPos(int num) {
  int i = 0;
  while (i < lineCount && lines[i].number < num) i++;
  return i;
}

static bool isBlank(const char* s) {
  for (; *s; s++) {
    if (!isspace((unsigned char)*s)) return false;
  }
  return true;
}

void programStoreSet(int num, const char* text) {
  int idx = findExactIndex(num);

  if (isBlank(text)) {
    if (idx >= 0) {
      for (int i = idx; i < lineCount - 1; i++) lines[i] = lines[i + 1];
      lineCount--;
    }
    return;
  }

  if (idx >= 0) {
    strncpy(lines[idx].text, text, MAX_PROGRAM_LINE_LEN - 1);
    lines[idx].text[MAX_PROGRAM_LINE_LEN - 1] = '\0';
    return;
  }

  if (lineCount >= MAX_PROGRAM_LINES) return;  // full -- silently ignored for now

  int pos = findInsertPos(num);
  for (int i = lineCount; i > pos; i--) lines[i] = lines[i - 1];
  lines[pos].number = num;
  strncpy(lines[pos].text, text, MAX_PROGRAM_LINE_LEN - 1);
  lines[pos].text[MAX_PROGRAM_LINE_LEN - 1] = '\0';
  lineCount++;
}

bool programStoreGetByIndex(int index, int* outNum, const char** outText) {
  if (index < 0 || index >= lineCount) return false;
  *outNum = lines[index].number;
  *outText = lines[index].text;
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

  for (int i = 0; i < lineCount; i++) {
    int n = snprintf(out + pos, outSize - pos, "L%d:\n", lines[i].number);
    if (n < 0 || pos + n >= outSize) return false;
    pos += n;

    rewriteGotoGosub(lines[i].text, rewritten, sizeof(rewritten));
    int n2 = snprintf(out + pos, outSize - pos, "%s\n", rewritten);
    if (n2 < 0 || pos + n2 >= outSize) return false;
    pos += n2;
  }
  out[pos] = '\0';
  return true;
}

bool programStoreSerialize(char* out, int outSize) {
  int pos = 0;
  for (int i = 0; i < lineCount; i++) {
    int n = snprintf(out + pos, outSize - pos, "%d %s\n", lines[i].number, lines[i].text);
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
    if (endptr != buf && num > 0) {
      const char* textStart = endptr;
      while (*textStart == ' ') textStart++;
      programStoreSet((int)num, textStart);
    }

    p = lineEnd ? lineEnd + 1 : p + len;
  }
}
