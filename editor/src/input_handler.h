#pragma once

#include "config.h"

void inputSetup();
void enqueueKeyEvent(uint8_t keyCode, uint8_t modifiers, bool pressed);
int processAllInput();
char hidToAscii(uint8_t hid, uint8_t modifiers);

// Drains the whole input queue looking for a pending break request --
// Escape or Ctrl+C, the two classic BASIC "stop the program" gestures --
// discarding everything else along the way. Used by mb_bridge's RUN loop
// to let a BLE-connected keyboard abort a runaway or input-waiting program.
// BLE keystrokes are enqueued from the BLE host's own task context (see
// ble_keyboard.cpp), independent of loopTask, so they still arrive in the
// queue even while loopTask itself is blocked deep inside mb_run(). A
// physical Back press can't do this today: it's only ever turned into a
// queued event by processPhysicalButtons(), which runs from loop() and is
// itself blocked for the same reason RUN needed this escape hatch.
bool inputConsumeBreakPending();
