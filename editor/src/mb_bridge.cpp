#include "mb_bridge.h"

#include "config.h"
#include "program_store.h"
#include "screen_editor.h"
#include "input_handler.h"
#include <my_basic.h>
#include <cstdarg>
#include <cstdio>
#include <Arduino.h>

static struct mb_interpreter_t* bas = 0;
static bool runAborted = false;

// Called before every statement while a program runs. mb_run() otherwise
// blocks loopTask for as long as the program takes to finish -- for a
// program with a loop (even a simple `10 GOTO 10`) that's forever, which
// starves the FreeRTOS idle task and trips the watchdog solid (confirmed
// on hardware: "Task watchdog got triggered ... IDLE (CPU 0)" repeating
// every ~10s, device fully unresponsive, not even the physical Back
// button). Yielding here periodically keeps the rest of the system alive
// (BLE, display) even mid-loop, and draining the input queue for a
// pending Escape/Ctrl+C lets a BLE-connected keyboard abort a runaway
// program -- see input_handler.h's inputConsumeBreakPending() for why
// physical Back can't do the same today.
static int mb_stepped_handler(struct mb_interpreter_t* s, void** l, const char* f, int p,
                               unsigned short row, unsigned short col) {
  (void)s; (void)l; (void)f; (void)p; (void)row; (void)col;
  static unsigned int stepCount = 0;
  if ((++stepCount & 0xFF) == 0) {
    vTaskDelay(1);
    if (inputConsumeBreakPending()) {
      runAborted = true;
      return MB_FUNC_ERR;
    }
  }
  // Time-throttled, not step-throttled -- a tight loop shouldn't repaint
  // faster than the e-ink panel can actually refresh (~400-650ms per the
  // panel's own timing), regardless of how many VM steps it takes per
  // iteration. See screen_editor.h's screenEditorFlushDisplay() comment.
  static unsigned long lastFlush = 0;
  unsigned long now = millis();
  if (now - lastFlush >= 500) {
    lastFlush = now;
    screenEditorFlushDisplay();
  }
  return MB_FUNC_OK;
}

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
  mb_debug_set_stepped_handler(bas, mb_stepped_handler, 0);
  mb_register_func(bas, "SCREEN", mb_screen_func);
  mb_register_func(bas, "CLS", mb_cls_func);
}

void mbBridgeRunDirect(const char* line) {
  int status = mb_load_string(bas, line, true);
  if (status == MB_FUNC_OK) {
    runAborted = false;
    mb_run(bas, true);
    if (runAborted) screenEditorTermPrintLine("?Break");
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
    runAborted = false;
    mb_run(bas, true);
    if (runAborted) screenEditorTermPrintLine("?Break");
  }
}
