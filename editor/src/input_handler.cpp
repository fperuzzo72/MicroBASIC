#include "input_handler.h"
#include "text_editor.h"
#include "file_manager.h"
#include "ble_keyboard.h"
#include "wifi_sync.h"
#include "dead_keys.h"
#include "screen_editor.h"
#include "program_store.h"
#include "mb_bridge.h"

#include <Arduino.h>
#include <SDCardManager.h>
#include <Utf8.h>
#include <cctype>
#include <cstdlib>
#include <strings.h>

// External variables
extern bool autoReconnectEnabled;
extern bool darkMode;
extern bool cleanMode;
extern bool deleteConfirmPending;
extern WritingMode writingMode;
extern FontSize fontSize;
extern bool showWordCount;

// External functions
void storePairedDevice(const std::string& address, const std::string& name);
bool getStoredDevice(std::string& address, std::string& name);
void clearStoredDevice();
uint32_t getCurrentPasskey();
bool isDeviceScanning();
void refreshScanNow();
void clearAllBluetoothBonds();

// --- Input Queue ---
static KeyEvent inputQueue[INPUT_QUEUE_SIZE];
static int queueHead = 0;
static int queueTail = 0;
static volatile bool queueFull = false;

// --- CapsLock state ---
static bool capsLockOn = false;

// Where to return after title edit is confirmed or cancelled
static UIState renameReturnState = UIState::FILE_BROWSER;

// Forward declaration
static void openTitleEdit(const char* currentTitle, UIState returnTo);

// OTA app detection (defined in main.cpp)
extern OtaAppEntry otaApps[];
extern int otaAppCount;
void switchToOtaApp(int index);

// Orientation helper (defined in main.cpp) — see its own comment for why
// SCREEN_EDITOR uses this directly instead of going through
// currentOrientation.
void applyOrientationToRenderer(Orientation o);

// --- Shared UI state (defined in main.cpp) ---
extern UIState currentState;
extern int mainMenuSelection;
extern int selectedFileIndex;
extern int settingsSelection;
extern int bluetoothDeviceSelection;
extern int pairedKeyboardSelection;
extern Orientation currentOrientation;
extern bool screenDirty;
extern char renameBuffer[];
extern int renameBufferLen;

void inputSetup() {
  queueHead = 0;
  queueTail = 0;
  queueFull = false;
  capsLockOn = false;
}

static bool isQueueEmpty() {
  return (queueHead == queueTail) && !queueFull;
}

void enqueueKeyEvent(uint8_t keyCode, uint8_t modifiers, bool pressed) {
  noInterrupts();
  if (!queueFull) {
    inputQueue[queueHead].keyCode = keyCode;
    inputQueue[queueHead].modifiers = modifiers;
    inputQueue[queueHead].pressed = pressed;
    queueHead = (queueHead + 1) % INPUT_QUEUE_SIZE;
    if (queueHead == queueTail) queueFull = true;
  }
  interrupts();
}

static KeyEvent dequeueKeyEvent() {
  KeyEvent event = {0, 0, false};
  noInterrupts();
  if (!isQueueEmpty()) {
    event = inputQueue[queueTail];
    queueTail = (queueTail + 1) % INPUT_QUEUE_SIZE;
    queueFull = false;
  }
  interrupts();
  return event;
}

char hidToAscii(uint8_t hid, uint8_t modifiers) {
  bool shifted = isShift(modifiers) ^ capsLockOn;

  // Letters a-z (HID 0x04-0x1D)
  if (hid >= 0x04 && hid <= 0x1D) {
    char base = 'a' + (hid - 0x04);
    return shifted ? (base - 32) : base;
  }

  // Number row (HID 0x1E-0x27)
  static const char unshifted[] = "1234567890";
  static const char shiftedNum[] = "!@#$%^&*()";
  if (hid >= 0x1E && hid <= 0x27) {
    int idx = hid - 0x1E;
    return isShift(modifiers) ? shiftedNum[idx] : unshifted[idx];
  }

  // Special keys
  switch (hid) {
    case 0x28: return '\n';  // Enter
    case 0x2B: return '\t';  // Tab
    case 0x2C: return ' ';   // Space

    // Symbol keys
    case 0x2D: return isShift(modifiers) ? '_' : '-';
    case 0x2E: return isShift(modifiers) ? '+' : '=';
    case 0x2F: return isShift(modifiers) ? '{' : '[';
    case 0x30: return isShift(modifiers) ? '}' : ']';
    case 0x31: return isShift(modifiers) ? '|' : '\\';
    case 0x33: return isShift(modifiers) ? ':' : ';';
    case 0x34: return isShift(modifiers) ? '"' : '\'';
    case 0x35: return isShift(modifiers) ? '~' : '`';
    case 0x36: return isShift(modifiers) ? '<' : ',';
    case 0x37: return isShift(modifiers) ? '>' : '.';
    case 0x38: return isShift(modifiers) ? '?' : '/';

    default: return 0;
  }
}

// Handle text editor input
static void handleEditorKey(uint8_t keyCode, uint8_t modifiers) {
  // Ctrl shortcuts
  if (isCtrl(modifiers)) {
    if (keyCode == HID_KEY_S) {
      saveCurrentFile();
      screenDirty = true;
      return;
    }
    if (keyCode == HID_KEY_Z) {
      cleanMode = !cleanMode;
      screenDirty = true;
      return;
    }
    if (keyCode == HID_KEY_N) {
      openTitleEdit(editorGetCurrentTitle(), UIState::TEXT_EDITOR);
      return;
    }
    if (keyCode == HID_KEY_T) {
      writingMode = (writingMode == WritingMode::TYPEWRITER) ? WritingMode::NORMAL : WritingMode::TYPEWRITER;
      screenDirty = true;
      return;
    }
    if (keyCode == HID_KEY_F) {
      int v = static_cast<int>(fontSize);
      fontSize = static_cast<FontSize>((v + 1) % 3);
      screenDirty = true;
      return;
    }
    if (keyCode == HID_KEY_W) {
      showWordCount = !showWordCount;
      screenDirty = true;
      return;
    }
    if (keyCode == HID_KEY_P) {
      writingMode = (writingMode == WritingMode::PAGINATION) ? WritingMode::NORMAL : WritingMode::PAGINATION;
      screenDirty = true;
      return;
    }
    if (keyCode == HID_KEY_A) {
      editorSelectAll();
      screenDirty = true;
      return;
    }
    if (keyCode == HID_KEY_C) {
      editorCopySelection();
      return;
    }
    if (keyCode == HID_KEY_X) {
      editorCutSelection();
      screenDirty = true;
      return;
    }
    if (keyCode == HID_KEY_V) {
      if (editorHasSelection()) editorDeleteSelection();
      editorPasteAtCursor();
      screenDirty = true;
      return;
    }
    // Ctrl+Left/Right: jump pages in pagination mode
    if (writingMode == WritingMode::PAGINATION) {
      int pageSize = editorGetStoredVisibleLines();
      if (keyCode == HID_KEY_LEFT) {
        for (int i = 0; i < pageSize; i++) editorMoveCursorUp();
        screenDirty = true;
        return;
      }
      if (keyCode == HID_KEY_RIGHT) {
        for (int i = 0; i < pageSize; i++) editorMoveCursorDown();
        screenDirty = true;
        return;
      }
    }
    return;
  }

  // ESC = save and return to file browser
  if (keyCode == HID_KEY_ESCAPE) {
    deadKeyReset();  // discard any pending dead key
    if (editorHasUnsavedChanges()) saveCurrentFile();
    currentState = UIState::FILE_BROWSER;
    screenDirty = true;
    return;
  }

  // Tab cycles writing modes
  if (keyCode == HID_KEY_TAB) {
    int v = static_cast<int>(writingMode);
    writingMode = static_cast<WritingMode>((v + 1) % 3);
    screenDirty = true;
    return;
  }

  // Navigation keys — Shift extends/keeps the selection, a plain move clears it
  {
    bool shift = isShift(modifiers);
    switch (keyCode) {
      case HID_KEY_LEFT:      editorMoveCursorLeft(shift);  screenDirty = true; return;
      case HID_KEY_RIGHT:     editorMoveCursorRight(shift); screenDirty = true; return;
      case HID_KEY_UP:        editorMoveCursorUp(shift);    screenDirty = true; return;
      case HID_KEY_DOWN:      editorMoveCursorDown(shift);  screenDirty = true; return;
      case HID_KEY_HOME:      editorMoveCursorHome(shift);  screenDirty = true; return;
      case HID_KEY_END:       editorMoveCursorEnd(shift);   screenDirty = true; return;
      case HID_KEY_BACKSPACE:
        if (editorHasSelection()) editorDeleteSelection(); else editorDeleteChar();
        screenDirty = true;
        return;
      case HID_KEY_DELETE:
        if (editorHasSelection()) editorDeleteSelection(); else editorDeleteForward();
        screenDirty = true;
        return;
      case HID_KEY_PAGE_UP:
      case HID_KEY_PAGE_DOWN: {
        // Typewriter mode only ever shows 1 line (editorSetVisibleLines(1)
        // in ui_renderer.cpp), so a "page" there has to mean something else
        // — editorGetPageJumpLines() is a real screenful either way.
        // Pagination mode's own linesPerPage IS editorGetStoredVisibleLines(),
        // so this doubles as an alias for its existing Ctrl+Left/Right jump.
        int jump = (writingMode == WritingMode::TYPEWRITER) ? editorGetPageJumpLines()
                                                              : editorGetStoredVisibleLines();
        if (jump < 1) jump = 1;
        for (int i = 0; i < jump; i++) {
          if (keyCode == HID_KEY_PAGE_UP) editorMoveCursorUp(shift);
          else editorMoveCursorDown(shift);
        }
        screenDirty = true;
        return;
      }
    }
  }

  // CapsLock toggle
  if (keyCode == HID_KEY_CAPSLOCK) {
    capsLockOn = !capsLockOn;
    return;
  }

  // Printable character — process through US-International dead key engine
  char c = hidToAscii(keyCode, modifiers);
  if (c != 0) {
    const char* composed = deadKeyProcess(c);
    if (composed == nullptr) {
      // No dead key involved: insert normally, replacing any selection
      if (editorHasSelection()) editorDeleteSelection();
      editorInsertChar(c);
      screenDirty = true;
    } else if (composed[0] != '\0') {
      // Composed result (or flushed dead key literal): insert UTF-8 string,
      // replacing any selection
      if (editorHasSelection()) editorDeleteSelection();
      editorInsertUtf8(composed);
      screenDirty = true;
      // If a non-composable char was requeued, insert it too
      char req = deadKeyTakeRequeue();
      if (req != 0) {
        editorInsertChar(req);
      }
    }
    // else: composed[0] == '\0' → dead key stored, nothing to insert yet
    // (leaves any existing selection untouched — nothing was typed)
  }
}

// Handle MicroBASIC SCREEN 1 grid editor input — first test pass, no
// interpreter, no selection/clipboard, just cursor movement and typing
// straight into the fixed 48x15 grid.
// Strips a pair of surrounding double quotes, if present ("MYPROG" -> MYPROG).
static void stripQuotes(const char* in, char* out, int outSize) {
  int len = strlen(in);
  if (len >= 2 && in[0] == '"' && in[len - 1] == '"') {
    len -= 2;
    if (len >= outSize) len = outSize - 1;
    memcpy(out, in + 1, len);
    out[len] = '\0';
  } else {
    strncpy(out, in, outSize - 1);
    out[outSize - 1] = '\0';
  }
}

// --- LIST/FILES pagination -------------------------------------------------
// MORE?-style: prints as many lines as fit on screen, then shows "MORE?"
// and waits -- any keypress clears it and resumes. Nothing here blocks;
// it's just state checked at the top of handleScreenEditorKey.

enum class PagingKind { NONE, LIST, FILES };
static PagingKind pagingKind = PagingKind::NONE;
static int pagingNext = 0;
static int pagingEnd = 0;  // LIST only, exclusive
static char pagingFiles[MAX_FILES][MAX_FILENAME_LEN];
static int pagingFileCount = 0;

static void printListLine(int idx) {
  int num = 0;
  const char* text = nullptr;
  if (!programStoreGetByIndex(idx, &num, &text)) return;
  char buf[MAX_PROGRAM_LINE_LEN + 16];
  snprintf(buf, sizeof(buf), "%d %s\n", num, text);
  screenEditorTermPrint(buf);
}

// Prints until the screen is full or the batch is done. Returns true if
// "MORE?" is now showing (paging still active), false if finished.
static bool pageBatch() {
  int total = (pagingKind == PagingKind::LIST) ? pagingEnd : pagingFileCount;
  while (pagingNext < total) {
    if (screenEditorRowsLeftOnScreen() < 2) {
      screenEditorTermPrint("MORE?");  // no trailing \n -- see continuePaging()
      return true;
    }
    if (pagingKind == PagingKind::LIST) {
      printListLine(pagingNext);
    } else {
      screenEditorTermPrint(pagingFiles[pagingNext]);
      screenEditorTermPrint("\n");
    }
    pagingNext++;
  }
  pagingKind = PagingKind::NONE;
  return false;
}

// The "MORE?" prompt sits alone on the row the cursor was on when
// pageBatch() printed it (nothing since has touched logical-line-start),
// so clearing the logical line clears exactly that row and puts the
// cursor back at its start, ready to keep printing from there.
static void continuePaging() {
  screenEditorClearLogicalLine();
  pageBatch();
}

static void startListCommand(const char* argStr) {
  int startNum = -1;
  int endNum = -1;
  bool hasRange = false;
  if (argStr && argStr[0]) {
    const char* dash = strchr(argStr, '-');
    if (dash) {
      startNum = atoi(argStr);
      endNum = atoi(dash + 1);
      hasRange = true;
    } else {
      startNum = atoi(argStr);
    }
  }

  int count = programStoreCount();
  int startIdx = 0;
  int endIdxExclusive = count;
  if (startNum >= 0) {
    while (startIdx < count) {
      int num;
      const char* text;
      programStoreGetByIndex(startIdx, &num, &text);
      if (num >= startNum) break;
      startIdx++;
    }
    if (hasRange) {
      endIdxExclusive = startIdx;
      while (endIdxExclusive < count) {
        int num;
        const char* text;
        programStoreGetByIndex(endIdxExclusive, &num, &text);
        if (num > endNum) break;
        endIdxExclusive++;
      }
    }
  }

  pagingKind = PagingKind::LIST;
  pagingNext = startIdx;
  pagingEnd = endIdxExclusive;
  screenEditorStartNewInputLine();
  pageBatch();
}

static void startFilesCommand() {
  pagingFileCount = 0;
  auto dir = SdMan.open("/MicroBASIC/programs");
  if (dir && dir.isDirectory()) {
    dir.rewindDirectory();
    char name[256];
    for (auto file = dir.openNextFile(); file; file = dir.openNextFile()) {
      file.getName(name, sizeof(name));
      int len = (int)strlen(name);
      if (name[0] != '.' && len > 4 && strcasecmp(name + len - 4, ".bas") == 0 &&
          pagingFileCount < MAX_FILES) {
        strncpy(pagingFiles[pagingFileCount], name, MAX_FILENAME_LEN - 1);
        pagingFiles[pagingFileCount][MAX_FILENAME_LEN - 1] = '\0';
        pagingFileCount++;
      }
      file.close();
    }
  }
  if (dir) dir.close();

  pagingKind = PagingKind::FILES;
  pagingNext = 0;
  screenEditorStartNewInputLine();
  pageBatch();
}

// --- Enter-key dispatch -----------------------------------------------------
// A logical line (screenEditorGetLogicalLineText() -- may span several
// physical rows if it wrapped while typing) is either:
//   - a numbered BASIC program line ("10 PRINT ...") -> program_store,
//     stays visible on screen exactly as typed;
//   - one of this environment's own direct-mode commands (MENU/NEW/RUN/
//     LIST/FILES/SAVE/LOAD) -- cleared from the visible terminal before
//     running, same reasoning as the very first LOAD/SAVE/MENU pass;
//   - anything else -> a My-Basic statement, executed immediately
//     (mbBridgeRunDirect -- variables persist across separate direct
//     statements, verified against the real interpreter).
static void executeLogicalLine(const char* line) {
  const char* p = line;
  while (*p == ' ') p++;

  if (isdigit((unsigned char)*p)) {
    char* endptr = nullptr;
    long num = strtol(p, &endptr, 10);
    const char* text = endptr;
    while (*text == ' ') text++;
    if (num < 1 || num > 65535) {
      // Line numbers are stored as uint16 (see program_store.h's record
      // layout); 0 is reserved as "no line" the way classic BASICs do.
      screenEditorStartNewInputLine();
      screenEditorTermPrintLine("?Line number out of range");
      return;
    }
    if (!programStoreSet((int)num, text)) {
      screenEditorStartNewInputLine();
      screenEditorTermPrintLine("?Out of memory");
      return;
    }
    screenEditorStartNewInputLine();
    return;
  }

  if (!*p) {
    screenEditorStartNewInputLine();
    return;
  }

  char upper[MAX_PROGRAM_LINE_LEN];
  int n = 0;
  for (; p[n] && n < (int)sizeof(upper) - 1; n++) upper[n] = (char)toupper((unsigned char)p[n]);
  upper[n] = '\0';

  auto isWord = [&](const char* w) { return strcmp(upper, w) == 0; };
  auto wordArg = [&](const char* w) -> const char* {
    size_t wl = strlen(w);
    if (strncmp(upper, w, wl) != 0) return nullptr;
    if (p[wl] != ' ' && p[wl] != '\0') return nullptr;
    const char* arg = p + wl;
    while (*arg == ' ') arg++;
    return arg;  // may point at '\0' if no argument was given
  };

  if (isWord("MENU")) {
    screenEditorClearLogicalLine();
    applyOrientationToRenderer(currentOrientation);
    currentState = UIState::MAIN_MENU;
    return;
  }
  if (isWord("NEW")) {
    programStoreClear();
    screenEditorClearLogicalLine();
    screenEditorStartNewInputLine();
    return;
  }
  if (isWord("RUN")) {
    screenEditorStartNewInputLine();
    mbBridgeRunProgram();
    screenEditorStartNewInputLine();
    return;
  }
  if (const char* arg = wordArg("LIST")) {
    screenEditorClearLogicalLine();
    startListCommand(arg);
    return;
  }
  if (wordArg("FILES")) {
    screenEditorClearLogicalLine();
    startFilesCommand();
    return;
  }
  if (const char* arg = wordArg("SAVE")) {
    static char nameBuf[MAX_FILENAME_LEN];
    stripQuotes(arg, nameBuf, sizeof(nameBuf));
    screenEditorClearLogicalLine();
    bool ok = screenEditorSaveProgram(nameBuf);
    screenEditorTermPrintLine(ok ? "Saved." : "?Save failed");
    return;
  }
  if (const char* arg = wordArg("LOAD")) {
    static char nameBuf[MAX_FILENAME_LEN];
    stripQuotes(arg, nameBuf, sizeof(nameBuf));
    screenEditorClearLogicalLine();
    bool ok = screenEditorLoadProgram(nameBuf);
    screenEditorTermPrintLine(ok ? "Loaded." : "?File not found");
    return;
  }

  // Not one of ours -- hand the whole line to My-Basic as a direct-mode statement.
  screenEditorStartNewInputLine();
  mbBridgeRunDirect(p);
  screenEditorStartNewInputLine();
}

static void handleScreenEditorKey(uint8_t keyCode, uint8_t modifiers) {
  if (pagingKind != PagingKind::NONE) {
    continuePaging();
    screenDirty = true;
    return;
  }

  if (keyCode == HID_KEY_ESCAPE) {
    deadKeyReset();  // discard any pending dead key
    applyOrientationToRenderer(currentOrientation);  // restore the real setting
    currentState = UIState::MAIN_MENU;
    screenDirty = true;
    return;
  }

  switch (keyCode) {
    case HID_KEY_LEFT:      screenEditorMoveCursor(0, -1);  screenDirty = true; return;
    case HID_KEY_RIGHT:     screenEditorMoveCursor(0, 1);   screenDirty = true; return;
    case HID_KEY_UP:        screenEditorMoveCursor(-1, 0);  screenDirty = true; return;
    case HID_KEY_DOWN:      screenEditorMoveCursor(1, 0);   screenDirty = true; return;
    case HID_KEY_HOME:      screenEditorGoHome();           screenDirty = true; return;
    case HID_KEY_END:       screenEditorGoEnd();            screenDirty = true; return;
    case HID_KEY_PAGE_UP:   screenEditorGoFirstRow();       screenDirty = true; return;
    case HID_KEY_PAGE_DOWN: screenEditorGoLastRow();        screenDirty = true; return;
    case HID_KEY_BACKSPACE: screenEditorBackspace();        screenDirty = true; return;
    case HID_KEY_ENTER: {
      char line[MAX_PROGRAM_LINE_LEN];
      screenEditorGetLogicalLineText(line, sizeof(line));
      executeLogicalLine(line);
      screenDirty = true;
      return;
    }
  }

  // Printable character — same US-International dead key engine as the
  // prose editor, so accented input works here too.
  char c = hidToAscii(keyCode, modifiers);
  if (c != 0) {
    const char* composed = deadKeyProcess(c);
    if (composed == nullptr) {
      screenEditorInsertCodepoint((uint32_t)(unsigned char)c);
      screenDirty = true;
    } else if (composed[0] != '\0') {
      const unsigned char* p = (const unsigned char*)composed;
      uint32_t cp = utf8NextCodepoint(&p);
      if (cp != 0) screenEditorInsertCodepoint(cp);
      screenDirty = true;
      char req = deadKeyTakeRequeue();
      if (req != 0) screenEditorInsertCodepoint((uint32_t)(unsigned char)req);
    }
    // else: composed[0] == '\0' -> dead key stored, nothing to insert yet
  }
}

// Open the title edit screen, returning to `returnTo` on confirm/cancel
static void openTitleEdit(const char* currentTitle, UIState returnTo) {
  strncpy(renameBuffer, currentTitle, MAX_TITLE_LEN - 1);
  renameBuffer[MAX_TITLE_LEN - 1] = '\0';
  renameBufferLen = strlen(renameBuffer);
  renameReturnState = returnTo;
  currentState = UIState::RENAME_FILE;
  screenDirty = true;
}

// Handle title edit input
static void handleRenameKey(uint8_t keyCode, uint8_t modifiers) {
  if (keyCode == HID_KEY_ENTER) {
    if (renameBufferLen > 0) {
      if (renameReturnState == UIState::TEXT_EDITOR) {
        editorSetCurrentTitle(renameBuffer);
        if (editorGetCurrentFile()[0] == '\0') {
          // New file — derive filename from title
          char filename[MAX_FILENAME_LEN];
          deriveUniqueFilename(renameBuffer, filename, MAX_FILENAME_LEN);
          editorSetCurrentFile(filename);
        } else {
          // Existing file — rename on disk to match new title
          updateFileTitle(editorGetCurrentFile(), renameBuffer);
        }
        editorSetUnsavedChanges(true);
        saveCurrentFile();
      } else {
        // Updating title of a file selected in the browser
        FileInfo* files = getFileList();
        updateFileTitle(files[selectedFileIndex].filename, renameBuffer);
      }
    }
    currentState = renameReturnState;
    screenDirty = true;
    return;
  }

  if (keyCode == HID_KEY_ESCAPE) {
    deadKeyReset();  // discard any pending dead key
    currentState = renameReturnState;
    screenDirty = true;
    return;
  }

  if (keyCode == HID_KEY_BACKSPACE) {
    deadKeyReset();  // dead key + backspace → discard dead key
    if (renameBufferLen > 0) {
      renameBufferLen--;
      renameBuffer[renameBufferLen] = '\0';
      screenDirty = true;
    }
    return;
  }

  // Allow all printable characters in a title (including spaces),
  // processed through the US-International dead key engine.
  char c = hidToAscii(keyCode, modifiers);
  if (c != 0 && c >= ' ') {
    const char* composed = deadKeyProcess(c);
    if (composed == nullptr) {
      // Normal character
      if (renameBufferLen < MAX_TITLE_LEN - 1) {
        renameBuffer[renameBufferLen++] = c;
        renameBuffer[renameBufferLen] = '\0';
        screenDirty = true;
      }
    } else if (composed[0] != '\0') {
      // Composed UTF-8 string (1–3 bytes typically): copy each byte
      for (const char* p = composed; *p != '\0' && renameBufferLen < MAX_TITLE_LEN - 1; p++) {
        renameBuffer[renameBufferLen++] = *p;
      }
      renameBuffer[renameBufferLen] = '\0';
      screenDirty = true;
      // If a non-composable char was requeued, insert it too
      char req = deadKeyTakeRequeue();
      if (req != 0 && req >= ' ' && renameBufferLen < MAX_TITLE_LEN - 1) {
        renameBuffer[renameBufferLen++] = req;
        renameBuffer[renameBufferLen] = '\0';
      }
    }
    // else: dead key stored, nothing to insert yet
  }
}

static void dispatchEvent(const KeyEvent& event) {
  if (!event.pressed) return;

  switch (currentState) {
    case UIState::MAIN_MENU: {
      // Base items: Browse Files, Screen Editor, New Program, Settings,
      // Sync -- keep in sync with ui_renderer.cpp's baseMenuItems[] /
      // BASE_MENU_COUNT.
      constexpr int BASE_MENU_COUNT = 5;
      int menuCount = BASE_MENU_COUNT + otaAppCount;
      if (event.keyCode == HID_KEY_DOWN) {
        mainMenuSelection = (mainMenuSelection + 1) % menuCount;
        screenDirty = true;
      } else if (event.keyCode == HID_KEY_UP) {
        mainMenuSelection = (mainMenuSelection - 1 + menuCount) % menuCount;
        screenDirty = true;
      } else if (event.keyCode == HID_KEY_ENTER) {
        if (mainMenuSelection == 0) {
          // MicroBASIC: the SCREEN 0-3 terminal -- see MicroBASIC repo's
          // docs/DEVELOPMENT_LOG.md. Only the terminal display resets;
          // whatever's in program_store (LOADed or typed earlier this
          // session) stays put.
          screenEditorReset();
          applyOrientationToRenderer(Orientation::LANDSCAPE_CCW);
          currentState = UIState::SCREEN_EDITOR;
          screenDirty = true;
        } else if (mainMenuSelection == 1) {
          refreshFileList();
          currentState = UIState::FILE_BROWSER;
          screenDirty = true;
        } else if (mainMenuSelection == 2) {
          // "New Program" -- the original prose editor ("New Note"),
          // renamed: a second, free-form way to write a program's source,
          // alongside the grid-faithful Screen Editor.
          createNewFile();
          openTitleEdit("Untitled", UIState::TEXT_EDITOR);
        } else if (mainMenuSelection == 3) {
          currentState = UIState::SETTINGS;
          screenDirty = true;
        } else if (mainMenuSelection == 4) {
          wifiSyncStart();
          currentState = UIState::WIFI_SYNC;
          screenDirty = true;
        } else if (mainMenuSelection >= BASE_MENU_COUNT) {
          switchToOtaApp(mainMenuSelection - BASE_MENU_COUNT);
        }
      }
      break;
    }

    case UIState::FILE_BROWSER: {
      int fc = getFileCount();

      // Delete confirmation pending — Enter confirms, anything else cancels
      if (deleteConfirmPending) {
        if (event.keyCode == HID_KEY_ENTER && fc > 0) {
          FileInfo* files = getFileList();
          deleteFile(files[selectedFileIndex].filename);
          int newFc = getFileCount();
          if (selectedFileIndex >= newFc) selectedFileIndex = newFc - 1;
          if (selectedFileIndex < 0) selectedFileIndex = 0;
        }
        deleteConfirmPending = false;
        screenDirty = true;
        break;
      }

      if (event.keyCode == HID_KEY_DOWN && fc > 0) {
        selectedFileIndex = (selectedFileIndex + 1) % fc;
        screenDirty = true;
      } else if (event.keyCode == HID_KEY_UP && fc > 0) {
        selectedFileIndex = (selectedFileIndex - 1 + fc) % fc;
        screenDirty = true;
      } else if (event.keyCode == HID_KEY_ENTER && fc > 0) {
        FileInfo* files = getFileList();
        loadFile(files[selectedFileIndex].filename);
        screenDirty = true;
      } else if (isCtrl(event.modifiers) && event.keyCode == HID_KEY_N) {
        if (fc > 0) {
          FileInfo* files = getFileList();
          openTitleEdit(files[selectedFileIndex].title, UIState::FILE_BROWSER);
        }
      } else if (isCtrl(event.modifiers) && event.keyCode == HID_KEY_D) {
        if (fc > 0) {
          deleteConfirmPending = true;
          screenDirty = true;
        }
      } else if (event.keyCode == HID_KEY_ESCAPE) {
        currentState = UIState::MAIN_MENU;
        screenDirty = true;
      }
      break;
    }

    case UIState::TEXT_EDITOR:
      handleEditorKey(event.keyCode, event.modifiers);
      break;

    case UIState::SCREEN_EDITOR:
      handleScreenEditorKey(event.keyCode, event.modifiers);
      break;

    case UIState::RENAME_FILE:
      handleRenameKey(event.keyCode, event.modifiers);
      break;

    case UIState::SETTINGS: {
      const int SETTINGS_COUNT = 6;  // Orientation, Dark Mode, Writing Mode, Font Size, Bluetooth, Paired Keyboards

      // Up/Down: navigate settings list (physical buttons also map here)
      if (event.keyCode == HID_KEY_DOWN) {
        settingsSelection = (settingsSelection + 1) % SETTINGS_COUNT;
        screenDirty = true;
      } else if (event.keyCode == HID_KEY_UP) {
        settingsSelection = (settingsSelection - 1 + SETTINGS_COUNT) % SETTINGS_COUNT;
        screenDirty = true;

      // Enter or Right: cycle setting forward
      } else if (event.keyCode == HID_KEY_ENTER || event.keyCode == HID_KEY_RIGHT) {
        if (settingsSelection == 0) {
          int v = static_cast<int>(currentOrientation);
          currentOrientation = static_cast<Orientation>((v + 1) % 4);
        } else if (settingsSelection == 1) {
          darkMode = !darkMode;
        } else if (settingsSelection == 2) {
          int v = static_cast<int>(writingMode);
          writingMode = static_cast<WritingMode>((v + 1) % 3);
        } else if (settingsSelection == 3) {
          int v = static_cast<int>(fontSize);
          fontSize = static_cast<FontSize>((v + 1) % 3);
        } else if (settingsSelection == 4) {
          currentState = UIState::BLUETOOTH_SETTINGS;
        } else if (settingsSelection == 5) {
          pairedKeyboardSelection = 0;
          currentState = UIState::PAIRED_KEYBOARDS;
        }
        screenDirty = true;

      // Left: cycle setting backward (keyboard only — physical L/R map to Up/Down)
      } else if (event.keyCode == HID_KEY_LEFT) {
        if (settingsSelection == 0) {
          int v = static_cast<int>(currentOrientation);
          currentOrientation = static_cast<Orientation>((v - 1 + 4) % 4);
        } else if (settingsSelection == 1) {
          darkMode = !darkMode;
        } else if (settingsSelection == 2) {
          int v = static_cast<int>(writingMode);
          writingMode = static_cast<WritingMode>((v - 1 + 3) % 3);
        } else if (settingsSelection == 3) {
          int v = static_cast<int>(fontSize);
          fontSize = static_cast<FontSize>((v - 1 + 3) % 3);
        }
        screenDirty = true;

      } else if (event.keyCode == HID_KEY_ESCAPE) {
        currentState = UIState::MAIN_MENU;
        screenDirty = true;
      }
      break;
    }

    case UIState::BLUETOOTH_SETTINGS: {
      int deviceCount = getDiscoveredDeviceCount();

      // Ensure selection is within bounds
      if (bluetoothDeviceSelection >= deviceCount && deviceCount > 0) {
        bluetoothDeviceSelection = deviceCount - 1;
      } else if (deviceCount == 0) {
        bluetoothDeviceSelection = 0; // Reset to 0 when no devices
      }

      if (event.keyCode == HID_KEY_ESCAPE) {
        DBG_PRINTLN("[INPUT] BT: Escape pressed - returning to settings");
        currentState = UIState::SETTINGS;
        screenDirty = true;
      } else if (event.keyCode == HID_KEY_DOWN) {
        if (deviceCount > 0) {
          bluetoothDeviceSelection = (bluetoothDeviceSelection + 1) % deviceCount;
          DBG_PRINTF("[INPUT] BT: Down pressed - selection now %d/%d\n", bluetoothDeviceSelection, deviceCount);
          screenDirty = true;
        }
      } else if (event.keyCode == HID_KEY_UP) {
        if (deviceCount > 0) {
          bluetoothDeviceSelection = (bluetoothDeviceSelection - 1 + deviceCount) % deviceCount;
          DBG_PRINTF("[INPUT] BT: Up pressed - selection now %d/%d\n", bluetoothDeviceSelection, deviceCount);
          screenDirty = true;
        }
      } else if (event.keyCode == HID_KEY_ENTER) {
        if (deviceCount > 0 && !isDeviceScanning()) {
          // Connect to the selected device
          connectToDevice(bluetoothDeviceSelection);
        } else if (!isDeviceScanning()) {
          // No devices — start a new scan
          startDeviceScan();
        }
        screenDirty = true;
      } else if (event.keyCode == HID_KEY_RIGHT) {
        // Right button = re-scan for devices
        if (!isDeviceScanning()) {
          startDeviceScan();
        }
        screenDirty = true;
      } else if (event.keyCode == HID_KEY_LEFT) {
        if (isKeyboardConnected()) {
          disconnectCurrentDevice();
          screenDirty = true;
        }
      }
      break;
    }

    case UIState::PAIRED_KEYBOARDS: {
      int count = getPairedKeyboardCount();

      if (event.keyCode == HID_KEY_ESCAPE) {
        currentState = UIState::SETTINGS;
        screenDirty = true;
      } else if (event.keyCode == HID_KEY_DOWN) {
        if (count > 0) {
          pairedKeyboardSelection = (pairedKeyboardSelection + 1) % count;
          screenDirty = true;
        }
      } else if (event.keyCode == HID_KEY_UP) {
        if (count > 0) {
          pairedKeyboardSelection = (pairedKeyboardSelection - 1 + count) % count;
          screenDirty = true;
        }
      } else if (event.keyCode == HID_KEY_ENTER) {
        if (count > 0) {
          connectToPairedKeyboard(pairedKeyboardSelection);
          currentState = UIState::SETTINGS;
          screenDirty = true;
        }
      } else if (event.keyCode == HID_KEY_D) {
        if (count > 0) {
          removePairedKeyboard(pairedKeyboardSelection);
          int newCount = getPairedKeyboardCount();
          if (pairedKeyboardSelection >= newCount && newCount > 0)
            pairedKeyboardSelection = newCount - 1;
          screenDirty = true;
        }
      } else if (event.keyCode == HID_KEY_LEFT) {
        // Disconnect if this keyboard is the currently connected one
        if (count > 0 && isKeyboardConnected()) {
          std::string addr, name; uint8_t addrType;
          getPairedKeyboard(pairedKeyboardSelection, addr, name, addrType);
          if (getCurrentDeviceAddress() == addr) {
            disconnectCurrentDevice();
            screenDirty = true;
          }
        }
      }
      break;
    }

    case UIState::WIFI_SYNC: {
      syncHandleKey(event.keyCode, event.modifiers);
      break;
    }

    default:
      break;
  }
}

bool inputConsumeBreakPending() {
  bool sawBreak = false;
  while (!isQueueEmpty()) {
    KeyEvent event = dequeueKeyEvent();
    if (!event.pressed) continue;
    if (event.keyCode == HID_KEY_ESCAPE) sawBreak = true;
    if (event.keyCode == HID_KEY_C && isCtrl(event.modifiers)) sawBreak = true;
  }
  return sawBreak;
}

int processAllInput() {
  int processedCount = 0;
  while (!isQueueEmpty()) {
    KeyEvent event = dequeueKeyEvent();
    dispatchEvent(event);
    processedCount++;
  }
  return processedCount;
}
