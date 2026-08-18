#include "mb_bridge.h"

#include "config.h"
#include "program_store.h"
#include "screen_editor.h"
#include <my_basic.h>
#include <cstdarg>
#include <cstdio>

static struct mb_interpreter_t* bas = 0;

static int mb_printer(struct mb_interpreter_t* s, const char* fmt, ...) {
  (void)s;
  char buf[256];
  va_list args;
  va_start(args, fmt);
  int n = vsnprintf(buf, sizeof(buf), fmt, args);
  va_end(args);
  screenEditorTermPrint(buf);
  return n;
}

static void mb_error_handler(struct mb_interpreter_t* s, mb_error_e e, const char* m, const char* f,
                              int p, unsigned short row, unsigned short col, int abort_code) {
  (void)s;
  (void)f;
  (void)p;
  (void)col;
  (void)abort_code;
  if (e == SE_NO_ERR) return;
  char buf[160];
  snprintf(buf, sizeof(buf), "?%s Ln %d\n", m ? m : "Error", row);
  screenEditorTermPrint(buf);
}

// SCREEN n -- switches the active SCREEN mode (0-3). See screen_editor.h.
static int mb_screen_func(struct mb_interpreter_t* s, void** l) {
  int result = MB_FUNC_OK;
  int_t n = 0;

  mb_check(mb_attempt_open_bracket(s, l));
  mb_check(mb_pop_int(s, l, &n));
  mb_check(mb_attempt_close_bracket(s, l));

  screenEditorSetMode((int)n);

  return result;
}

// CLS -- clears the terminal (not the program store).
static int mb_cls_func(struct mb_interpreter_t* s, void** l) {
  int result = MB_FUNC_OK;

  mb_check(mb_attempt_open_bracket(s, l));
  mb_check(mb_attempt_close_bracket(s, l));

  screenEditorReset();

  return result;
}

void mbBridgeSetup() {
  mb_init();
  mb_open(&bas);
  mb_set_printer(bas, mb_printer);
  mb_set_error_handler(bas, mb_error_handler);
  mb_register_func(bas, "SCREEN", mb_screen_func);
  mb_register_func(bas, "CLS", mb_cls_func);
}

void mbBridgeRunDirect(const char* line) {
  int status = mb_load_string(bas, line, true);
  if (status == MB_FUNC_OK) {
    mb_run(bas, true);
  }
}

void mbBridgeRunProgram() {
  static char source[PROGRAM_TEXT_BUFFER_SIZE];
  if (!programStoreBuildRunSource(source, sizeof(source))) {
    screenEditorTermPrintLine("?Program too large to run");
    return;
  }

  mb_reset(&bas, false, true);  // fresh variables (classic RUN semantics), keep registered functions
  int status = mb_load_string(bas, source, true);
  if (status == MB_FUNC_OK) {
    mb_run(bas, true);
  }
}
