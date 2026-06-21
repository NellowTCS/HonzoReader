#include <globals.h>
#include <SD_MMC.h>
#include <cstring>
#include <cctype>
#include <Fonts/FreeSerifBold12pt8b.h>
#include <Fonts/FreeSerif9pt8b.h>
#include "reader.h"
#include "honzo_meta.h"

// Bookmark save/load
bool saveBookmark() {
    SD_MMC.mkdir(BMARKS_DIR);
    char path[256];
    snprintf(path, sizeof(path), "%s/%s.bmark", BMARKS_DIR, g_curBook);
    // Replace any extension with .bmark
    char* dot = strrchr(path, '.');
    if (dot && dot > strrchr(path, '/')) {
        strcpy(dot, ".bmark");
    } else {
        strcat(path, ".bmark");
    }

    File f = SD_MMC.open(path, FILE_WRITE);
    if (!f) return false;
    f.printf("%u %u\n", g_curChapter, g_curPage);
    f.close();
    return true;
}

bool loadBookmark(Bookmark* bm) {
    char path[256];
    snprintf(path, sizeof(path), "%s/%s.bmark", BMARKS_DIR, g_curBook);
    char* dot = strrchr(path, '.');
    if (dot && dot > strrchr(path, '/')) {
        strcpy(dot, ".bmark");
    } else {
        strcat(path, ".bmark");
    }

    File f = SD_MMC.open(path, FILE_READ);
    if (!f) return false;
    String s = f.readStringUntil('\n');
    f.close();
    int c = -1, p = -1;
    sscanf(s.c_str(), "%d %d", &c, &p);
    if (c >= 0 && p >= 0) {
        bm->chapter = (uint16_t)c;
        bm->page    = (uint16_t)p;
        return true;
    }
    return false;
}

void saveCurrentBook() {
    File f = SD_MMC.open(CUR_PATH, FILE_WRITE);
    if (f) { f.print(g_curBook); f.close(); }
}

void loadCurrentBook() {
    if (!SD_MMC.exists(CUR_PATH)) return;
    File f = SD_MMC.open(CUR_PATH, FILE_READ);
    if (!f) return;
    String s = f.readStringUntil('\n');
    f.close();
    s.trim();
    if (s.length() > 0) {
        strncpy(g_curBook, s.c_str(), sizeof(g_curBook) - 1);
    }
}

// Metadata cache: save/load book titles/authors
static void save_meta_cache() {
    File f = SD_MMC.open(META_CACHE, FILE_WRITE);
    if (!f) return;
    for (int i = 0; i < g_bookCount; i++) {
        f.printf("%s|%s|%s\n", g_books[i].name, g_books[i].title, g_books[i].author);
    }
    f.close();
}

static void load_meta_cache() {
    if (!SD_MMC.exists(META_CACHE)) return;
    File f = SD_MMC.open(META_CACHE, FILE_READ);
    if (!f) return;
    for (int i = 0; i < g_bookCount && f.available(); ) {
        String line = f.readStringUntil('\n');
        line.trim();
        // Split on |
        int p1 = line.indexOf('|');
        if (p1 < 0) continue;
        int p2 = line.indexOf('|', p1 + 1);
        String fname = line.substring(0, p1);
        // Match by filename
        if (strcmp(fname.c_str(), g_books[i].name) != 0) { i++; continue; }
        String t = (p2 > p1) ? line.substring(p1 + 1, p2) : "";
        String a = (p2 > 0 && p2 + 1 < (int)line.length()) ? line.substring(p2 + 1) : "";
        strncpy(g_books[i].title, t.c_str(), sizeof(g_books[0].title) - 1);
        strncpy(g_books[i].author, a.c_str(), sizeof(g_books[0].author) - 1);
        i++;
    }
    f.close();
}

// Library: scan SD for .hzo files
void library_init() {
    g_appMode     = MODE_LIBRARY;
    g_bookCount   = 0;
    g_selIndex    = 0;

    if (!SD_MMC.begin()) {
        vTaskDelay(pdMS_TO_TICKS(100));
        if (!SD_MMC.begin()) return;
    }

    SD_MMC.mkdir(BOOKS_DIR);
    SD_MMC.mkdir(BMARKS_DIR);

    File dir = SD_MMC.open(BOOKS_DIR);
    if (!dir || !dir.isDirectory()) return;

    File entry;
    while ((entry = dir.openNextFile()) && g_bookCount < 128) {
        if (!entry.isDirectory()) {
            const char* name = entry.name();
            // Skip macOS Apple Double metacache
            if (name[0] == '.' && name[1] == '_') { entry.close(); continue; }
            const char* ext  = strrchr(name, '.');
            if (ext && (strcasecmp(ext, ".hzo") == 0)) {
                strncpy(g_books[g_bookCount].name, name, sizeof(g_books[0].name) - 1);
                g_books[g_bookCount].title[0] = '\0';
                g_books[g_bookCount].author[0] = '\0';
                g_bookCount++;
            }
        }
        entry.close();
    }
    dir.close();

    // Load cached metadata
    load_meta_cache();

    // Extract metadata for any books missing title
    bool need_save = false;
    for (int i = 0; i < g_bookCount; i++) {
        if (g_books[i].title[0] == '\0') {
            char full_path[256];
            snprintf(full_path, sizeof(full_path), "/sdcard/books/%s", g_books[i].name);
            if (honzo_extract_meta(full_path, g_books[i].title, sizeof(g_books[0].title),
                                   g_books[i].author, sizeof(g_books[0].author))) {
                need_save = true;
            }
        }
        // Fallback: use filename without extension
        if (g_books[i].title[0] == '\0') {
            strncpy(g_books[i].title, g_books[i].name, sizeof(g_books[0].title) - 1);
            char* dot = strrchr(g_books[i].title, '.');
            if (dot) *dot = '\0';
        }
    }
    if (need_save) save_meta_cache();

    if (g_bookCount > 0) {
        loadCurrentBook();
        for (int i = 0; i < g_bookCount; i++) {
            if (strcmp(g_books[i].name, g_curBook) == 0) {
                g_selIndex = i;
                break;
            }
        }
    }

    g_needsRedraw = true;
}

void library_process_key(char ch) {
    if (ch == 21) {  // RIGHT is next
        if (g_selIndex < g_bookCount - 1) {
            g_selIndex++;
            g_needsRedraw = true;
        }
    } else if (ch == 19) {  // LEFT is prev
        if (g_selIndex > 0) {
            g_selIndex--;
            g_needsRedraw = true;
        }
    } else if (ch == 32 || ch == 13) {  // Space or Enter is open
        if (g_bookCount > 0) {
            strncpy(g_curBook, g_books[g_selIndex].name, sizeof(g_curBook) - 1);
            saveCurrentBook();
            open_book(g_books[g_selIndex].name);
        }
    } else if (ch == '?') {
        g_prevMode = g_appMode;
        g_appMode = MODE_HELP;
        g_needsRedraw = true;
    }
}

// Library: render book picker
void library_render() {
    display.fillScreen(GxEPD_WHITE);

    display.setFont(&FreeSerifBold12pt8b);
    display.setCursor(8, 18);
    display.print("Honzo Reader");
    display.drawFastHLine(0, 22, display.width(), GxEPD_BLACK);

    if (g_bookCount == 0) {
        display.setFont(&FreeSerif9pt8b);
        display.setCursor(8, 55);
        display.print("No .hzo files found");
        display.setCursor(8, 75);
        display.print("Place .hzo files in /books/ on SD");
        EINK().refresh();
        return;
    }

    display.setFont(&FreeSerif9pt8b);
    int y = 38;
    int visStart = max(0, g_selIndex - 8);
    int visEnd = min(g_bookCount, visStart + 13);

    for (int i = visStart; i < visEnd; i++) {
        if (i == g_selIndex) {
            display.fillRect(0, y - 10, display.width(), 16, GxEPD_BLACK);
            display.setTextColor(GxEPD_WHITE);
        } else {
            display.setTextColor(GxEPD_BLACK);
        }

        display.setCursor(8, y);
        String title = g_books[i].title;
        if ((int)title.length() > 38) title = title.substring(0, 36) + "..";
        display.print(title);
        y += 16;
    }

    display.setTextColor(GxEPD_BLACK);
    display.setFont(&Font5x7Fixed);
    display.setCursor(4, display.height() - 4);
    display.print("< > select  SPC open  FN+< exit");

    if (g_bookCount > 13) {
        int barH = display.height() - 42;
        int thumbH = max(barH * 13 / g_bookCount, 6);
        int thumbY = 24 + (barH - thumbH) * g_selIndex / max(g_bookCount - 1, 1);
        display.drawFastVLine(display.width() - 4, 24, barH, GxEPD_BLACK);
        display.fillRect(display.width() - 6, thumbY, 4, thumbH, GxEPD_BLACK);
    }

    EINK().refresh();
}
