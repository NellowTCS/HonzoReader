#include <globals.h>
#include <SD_MMC.h>
#include <cstring>
#include <cctype>
#include <Fonts/FreeSerifBold12pt8b.h>
#include <Fonts/FreeSerif9pt8b.h>
#include "reader.h"

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

// Library: scan SD for .epub files
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
    while ((entry = dir.openNextFile()) && g_bookCount < 32) {
        if (!entry.isDirectory()) {
            const char* name = entry.name();
            const char* ext  = strrchr(name, '.');
            if (ext && (strcasecmp(ext, ".epub") == 0)) {
                strncpy(g_books[g_bookCount].name, name, sizeof(g_books[0].name) - 1);
                g_bookCount++;
            }
        }
        entry.close();
    }
    dir.close();

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
    }
}

// Library: render book picker
void library_render() {
    display.fillScreen(GxEPD_WHITE);

    display.setFont(&FreeSerifBold12pt8b);
    display.setCursor(8, 18);
    display.print("ePub Reader");
    display.drawFastHLine(0, 22, display.width(), GxEPD_BLACK);

    if (g_bookCount == 0) {
        display.setFont(&FreeSerif9pt8b);
        display.setCursor(8, 55);
        display.print("No .epub files found");
        display.setCursor(8, 75);
        display.print("Place them in /books/ on SD");
        EINK().refresh();
        return;
    }

    display.setFont(&FreeSerif9pt8b);
    int y = 38;
    int visStart = max(0, g_selIndex - 8);
    int visEnd = min(g_bookCount, visStart + 14);

    for (int i = visStart; i < visEnd; i++) {
        if (i == g_selIndex) {
            display.fillRect(0, y - 10, display.width(), 16, GxEPD_BLACK);
            display.setTextColor(GxEPD_WHITE);
        } else {
            display.setTextColor(GxEPD_BLACK);
        }

        display.setCursor(8, y);
        String name = g_books[i].name;
        if (name.endsWith(".epub")) name = name.substring(0, name.length() - 5);
        if ((int)name.length() > 38) name = name.substring(0, 36) + "..";
        display.print(name);
        y += 16;
    }

    display.setTextColor(GxEPD_BLACK);
    display.setFont(&Font5x7Fixed);
    display.setCursor(4, display.height() - 4);
    display.print("< > select  SPC open  FN+< exit");

    if (g_bookCount > 14) {
        int barH = display.height() - 26;
        int thumbH = max(barH * 14 / g_bookCount, 6);
        int thumbY = 24 + (barH - thumbH) * g_selIndex / max(g_bookCount - 1, 1);
        display.drawFastVLine(display.width() - 4, 24, barH, GxEPD_BLACK);
        display.fillRect(display.width() - 6, thumbY, 4, thumbH, GxEPD_BLACK);
    }

    EINK().refresh();
}
