#include "file_manager.h"
#include "text_editor.h"
#include <Arduino.h>
#include <SDCardManager.h>
#include <cstring>

// --- Collections (see file_manager.h) ---
struct CollectionInfo {
  const char* dir;
  const char* ext;      // extension given to files created here
  const char* fallback; // base name when a title sanitises down to nothing
  const char* label;
  bool extFilter;       // list only *ext, or everything the folder holds
};

// Programs are listed unfiltered on purpose: BASIC's SAVE stores under
// exactly the name typed, with no extension forced on, so a folder of
// perfectly good programs can contain no .bas at all.
static const CollectionInfo COLLECTIONS[] = {
  {"/notes",               ".txt", "note",    "Notes",    true},
  {"/MicroBASIC/programs", ".bas", "program", "Programs", false},
};

static FileCollection currentCollection = FileCollection::NOTES;
static const CollectionInfo& coll() { return COLLECTIONS[(int)currentCollection]; }

// --- File list ---
static FileInfo fileList[MAX_FILES];
static int fileCount = 0;

// Shared state
extern UIState currentState;

// Convert filename to a readable display title.
// "my_note_2.txt" -> "My Note 2"
static void filenameToTitle(const char* filename, char* out, int maxLen) {
  int j = 0;
  bool capitalizeNext = true;
  for (int i = 0; filename[i] != '\0' && filename[i] != '.' && j < maxLen - 1; i++) {
    char c = filename[i];
    if (c == '_') {
      if (j > 0) out[j++] = ' ';
      capitalizeNext = true;
    } else {
      if (capitalizeNext && c >= 'a' && c <= 'z') c -= 32;
      capitalizeNext = false;
      out[j++] = c;
    }
  }
  out[j] = '\0';
  if (j == 0) strncpy(out, "Untitled", maxLen - 1);
}

// Convert a title to a valid FAT filename (lowercase, spaces->underscores,
// non-alphanumeric stripped, the collection's extension appended).
static void titleToFilename(const char* title, char* out, int maxLen) {
  const CollectionInfo& c = coll();
  const int extLen = (int)strlen(c.ext);
  int maxBase = maxLen - extLen - 1;
  int j = 0;
  for (int i = 0; title[i] != '\0' && j < maxBase; i++) {
    char c = title[i];
    if (c >= 'A' && c <= 'Z') c += 32;
    if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) {
      out[j++] = c;
    } else if (c == ' ' || c == '_' || c == '-') {
      if (j > 0 && out[j - 1] != '_') out[j++] = '_';
    }
  }
  while (j > 0 && out[j - 1] == '_') j--;
  if (j == 0) { strncpy(out, c.fallback, maxLen - 1); j = (int)strlen(out); }
  strcpy(out + j, c.ext);
}

// Derive a unique filename in the current collection from a title, handling
// collisions with a _2, _3 suffix.
void deriveUniqueFilename(const char* title, char* out, int maxLen) {
  titleToFilename(title, out, maxLen);

  char path[320];
  snprintf(path, sizeof(path), "%s/%s", coll().dir, out);
  if (!SdMan.exists(path)) return;

  // Collision — strip .txt, try _2, _3 ...
  char base[MAX_FILENAME_LEN];
  strncpy(base, out, maxLen - 1);
  base[maxLen - 1] = '\0';
  const size_t extLen = strlen(coll().ext);
  if (strlen(base) > extLen) base[strlen(base) - extLen] = '\0';

  int suffix = 2;
  while (SdMan.exists(path) && suffix <= 99) {
    snprintf(out, maxLen, "%s_%d%s", base, suffix++, coll().ext);
    snprintf(path, sizeof(path), "%s/%s", coll().dir, out);
  }
}

void setFileCollection(FileCollection c) {
  if (currentCollection == c) return;
  currentCollection = c;
  refreshFileList();
}

FileCollection getFileCollection() { return currentCollection; }
const char* fileCollectionName() { return coll().label; }

void fileManagerSetup() {
  if (!SdMan.begin()) {
    DBG_PRINTLN("SD Card mount failed!");
    return;
  }

  for (const CollectionInfo& c : COLLECTIONS) {
    if (!SdMan.exists(c.dir)) SdMan.mkdir(c.dir);
  }

  DBG_PRINTLN("SD Card initialized");
  refreshFileList();
}

void refreshFileList() {
  fileCount = 0;

  auto root = SdMan.open(coll().dir);
  if (!root || !root.isDirectory()) {
    if (root) root.close();
    return;
  }

  root.rewindDirectory();
  char name[256];

  for (auto file = root.openNextFile(); file; file = root.openNextFile()) {
    file.getName(name, sizeof(name));
    if (name[0] == '.' || fileCount >= MAX_FILES) {
      file.close();
      if (fileCount >= MAX_FILES) break;
      continue;
    }

    // The save path writes .tmp and rotates the previous version to .bak;
    // neither is a document, so neither belongs in a list the user picks from.
    const int nameLen = strlen(name);
    const bool scratch = nameLen > 4 && (strcasecmp(name + nameLen - 4, ".bak") == 0 ||
                                         strcasecmp(name + nameLen - 4, ".tmp") == 0);
    const bool matches = !coll().extFilter ||
                         (nameLen > 4 && strcasecmp(name + nameLen - 4, coll().ext) == 0);
    if (!scratch && matches) {
      strncpy(fileList[fileCount].filename, name, MAX_FILENAME_LEN - 1);
      fileList[fileCount].filename[MAX_FILENAME_LEN - 1] = '\0';

      filenameToTitle(name, fileList[fileCount].title, MAX_TITLE_LEN);
      fileList[fileCount].modTime = 0;
      fileCount++;
    }
    file.close();
  }
  root.close();
  SdMan.sleep();

  DBG_PRINTF("File listing: %d files found\n", fileCount);
}

int getFileCount() { return fileCount; }
FileInfo* getFileList() { return fileList; }

void loadFile(const char* filename) {
  char path[320];
  snprintf(path, sizeof(path), "%s/%s", coll().dir, filename);

  auto file = SdMan.open(path, O_RDONLY);
  if (!file) {
    DBG_PRINTF("Could not open: %s\n", path);
    return;
  }

  // Silently truncates a file larger than TEXT_BUFFER_SIZE — no warning
  // shown, and saveCurrentFile() below will write this truncated buffer
  // back over the original on the next save. See the TEXT_BUFFER_SIZE
  // comment in config.h — TODO, revisit.
  char* buf = editorGetBuffer();
  int readResult = file.read(buf, TEXT_BUFFER_SIZE - 1);
  size_t bytesRead = (readResult > 0) ? (size_t)readResult : 0;
  buf[bytesRead] = '\0';
  file.close();

  editorSetCurrentFile(filename);
  editorLoadBuffer(bytesRead);

  // Title comes from the filename, not the file content
  char title[MAX_TITLE_LEN];
  filenameToTitle(filename, title, MAX_TITLE_LEN);
  editorSetCurrentTitle(title);
  editorSetUnsavedChanges(false);

  currentState = UIState::TEXT_EDITOR;
  SdMan.sleep();
  DBG_PRINTF("Loaded: %s (%d bytes)\n", filename, (int)bytesRead);
}

void saveCurrentFile(bool refreshList) {
  const char* filename = editorGetCurrentFile();
  if (filename[0] == '\0') return;

  char path[320], tmpPath[336], bakPath[336];
  snprintf(path, sizeof(path), "%s/%s", coll().dir, filename);
  snprintf(tmpPath, sizeof(tmpPath), "%s.tmp", path);
  snprintf(bakPath, sizeof(bakPath), "%s.bak", path);

  // Step 1: Write new content to .tmp
  auto file = SdMan.open(tmpPath, O_WRONLY | O_CREAT | O_TRUNC);
  if (!file) {
    DBG_PRINTF("saveCurrentFile: could not create tmp: %s\n", tmpPath);
    return;
  }

  size_t toWrite = editorGetLength();
  size_t written = file.write((const uint8_t*)editorGetBuffer(), toWrite);
  file.close();

  // Step 2: Verify bytes written match expected length
  if (written != toWrite) {
    DBG_PRINTF("saveCurrentFile: write mismatch (%d/%d) — aborting\n", (int)written, (int)toWrite);
    SdMan.remove(tmpPath);
    return;
  }

  // Step 3: Rotate original → .bak (original is now safe in .tmp, preserve previous .bak)
  if (SdMan.exists(path)) {
    SdMan.remove(bakPath);          // Remove old .bak (if any)
    SdMan.rename(path, bakPath);    // Original becomes new .bak
  }

  // Step 4: Promote .tmp → original
  SdMan.rename(tmpPath, path);

  editorSetUnsavedChanges(false);
  if (refreshList) refreshFileList();
  SdMan.sleep();
  DBG_PRINTF("Saved: %s\n", filename);
}

void createNewFile() {
  editorClear();
  editorSetCurrentFile("");       // filename derived from title when user confirms
  editorSetCurrentTitle("Untitled");
  editorSetUnsavedChanges(true);
}

// Rename a file on disk to match a new title, updating editor state if needed.
void updateFileTitle(const char* filename, const char* newTitle) {
  char newFilename[MAX_FILENAME_LEN];
  deriveUniqueFilename(newTitle, newFilename, MAX_FILENAME_LEN);

  if (strcmp(newFilename, filename) != 0) {
    char oldPath[320], newPath[320];
    snprintf(oldPath, sizeof(oldPath), "%s/%s", coll().dir, filename);
    snprintf(newPath, sizeof(newPath), "%s/%s", coll().dir, newFilename);
    SdMan.rename(oldPath, newPath);

    if (strcmp(editorGetCurrentFile(), filename) == 0) {
      editorSetCurrentFile(newFilename);
    }
  }

  refreshFileList();
  SdMan.sleep();
}

void deleteFile(const char* filename) {
  char path[320], bakPath[336];
  snprintf(path, sizeof(path), "%s/%s", coll().dir, filename);
  snprintf(bakPath, sizeof(bakPath), "%s.bak", path);
  SdMan.remove(path);
  SdMan.remove(bakPath);
  refreshFileList();
  SdMan.sleep();
  DBG_PRINTF("Deleted: %s\n", filename);
}
