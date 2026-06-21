#pragma once

#include <Arduino.h>
#include <cstring>
#include <Adafruit_GFX.h>

// SD paths
static constexpr const char* BOOKS_DIR    = "/books";
static constexpr const char* BMARKS_DIR   = "/books/.bmarks";
static constexpr const char* CUR_PATH     = "/books/.current";
static constexpr const char* META_CACHE   = "/books/.meta";

// App modes
enum AppMode : uint8_t {
    MODE_LIBRARY,
    MODE_READING,
    MODE_TOC,
    MODE_JUMP,
    MODE_HELP,
};

// Book entry
struct BookEntry {
    char name[128];
    char title[128];
    char author[64];
};

// Bookmark
struct Bookmark {
    uint16_t chapter;
    uint16_t page;
};

// Styled text run for rendering
struct StyledRun {
    const char* text;
    uint16_t    len;
    uint8_t     font;     // 0=body, 1=bold, 2=italic, 3=bold-italic
    uint8_t     heading;  // 0=not heading, 1-6 = h1-h6
    uint8_t     flags;    // bit 0:code, bit 1:quote, bit 2:list
    uint16_t    indent;
};

// Display line
static constexpr int MAX_RUNS_PER_LINE = 8;
struct LayoutLine {
    StyledRun runs[MAX_RUNS_PER_LINE];
    uint8_t   runCount;
    uint16_t  height;  // line height in pixels
    uint8_t   indentLevel;
};

// Page
static constexpr int MAX_LINES_PER_PAGE = 15;
struct LayoutPage {
    LayoutLine lines[MAX_LINES_PER_PAGE];
    uint8_t    lineCount;
};

// App globals
extern AppMode     g_appMode;
extern AppMode     g_prevMode;
extern BookEntry   g_books[128];
extern int         g_bookCount;
extern int         g_selIndex;

extern char        g_curBook[128];
extern char        g_bookTitle[128];
extern char        g_bookAuthor[64];
extern uint16_t    g_curChapter;
extern uint16_t    g_curPage;
extern uint16_t    g_chapterCount;
extern uint16_t    g_pagesInChapter;
extern uint16_t    g_totalPages;
extern uint32_t    g_totalWords;
extern char*       g_plainText;       // current chapter plain text
extern size_t      g_plainTextLen;
extern LayoutPage* g_pages;           // rendered pages for current chapter
extern uint16_t    g_pageCount;

extern char        g_jumpBuf[8];
extern uint8_t     g_jumpLen;

extern int32_t     g_touchBase;
extern int32_t     g_scrollAccum;
extern bool        g_needsRedraw;
extern char        g_boundaryMsg[32];

// TOC entry
struct TocEntry {
    uint16_t index;
    char     title[64];
};

extern TocEntry    g_toc[64];
extern uint16_t    g_tocCount;
extern uint16_t    g_tocScroll;

// API
bool saveBookmark();
bool loadBookmark(Bookmark* bm);
void saveCurrentBook();
void loadCurrentBook();

void library_init();
void library_process_key(char ch);
void library_render();

void open_book(const char* filename);
void reader_load_chapter(uint16_t chapter);
void reader_process_key(char ch);
void reader_render();

void toc_process_key(char ch);
void toc_render();

void jump_process_key(char ch);
void jump_render();
void help_render();

void touch_init();
void touch_scroll();

void render_chapter_markup(const char* text, size_t len, uint8_t markup);
void render_page_to_eink(uint16_t pageIdx);
void updateOLED();

