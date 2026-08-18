#pragma once

// Thin bridge between My-Basic (editor/lib/MyBasic) and the Screen Editor
// terminal. See docs/DEVELOPMENT_LOG.md for why GOTO/GOSUB targets get
// rewritten to alphabetic labels (My-Basic doesn't support bare numeric
// labels -- verified against the real interpreter, not assumed) and for
// the SCREEN/CLS built-ins this registers.

void mbBridgeSetup();  // call once at boot

// Executes one direct-mode statement immediately. Variable state persists
// across calls (verified against the real interpreter) -- this is what
// gives typing `A = 5` then `PRINT A` on separate lines the classic
// immediate-mode REPL behavior. Output goes to the Screen Editor terminal.
void mbBridgeRunDirect(const char* line);

// RUN: resets variables to a clean slate (classic BASIC RUN semantics,
// unlike direct-mode statements), builds the run source from the whole
// program_store, and executes it. Output streams to the terminal as it runs.
void mbBridgeRunProgram();
