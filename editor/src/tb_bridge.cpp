#include "tb_bridge.h"

#include "config.h"
#include "screen_editor.h"

extern bool screenDirty;

#include <cstring>

#include "tb_interp.h"

static bool ready = false;

void tbSetup() {
  if (ready) return;
  basicSetup();
  ready = true;
}

bool tbExecuteLine(const char* line) {
  if (!ready) tbSetup();
  if (!line) return true;

  // ibuffer is length-prefixed, not NUL-terminated-at-zero: ins() writes the
  // length into [0] and the text from [1], with a NUL after it. Anything that
  // fills this buffer has to follow that convention or the tokeniser reads
  // the first character as a count. (Discovered from consins() in upstream's
  // own runtime, not from documentation.)
  size_t len = strlen(line);
  if (len > BUFSIZE - 3) len = BUFSIZE - 3;
  ibuffer[0] = (char)(unsigned char)len;
  memcpy(ibuffer + 1, line, len);
  ibuffer[len + 1] = '\0';

  // From here down this mirrors basicLoop() after its ins() call. Deliberately
  // kept in the same order and with the same assignments, so that when
  // upstream changes its REPL this is easy to diff against.
  iodefaults();
  form = 0;

  bi = ibuffer;
  nexttoken();

  if (token == NUMBER) {
    // A numbered line is program text, not a command: it goes to the
    // interpreter's own tokenised program memory.
    ax = x;
    storeline();
  } else {
    statement();
    st = SINT;
  }

  screenDirty = true;
  const bool failed = (er != 0);
  if (failed) {
    // The interpreter has already printed its message through outch() by now;
    // this just clears the flag so the next line starts clean.
    reseterror();
  }

  return !failed;
}
