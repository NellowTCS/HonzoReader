#include "reader.h"

AppMode     g_appMode       = MODE_LIBRARY;
BookEntry   g_books[32]     = {};
int         g_bookCount     = 0;
int         g_selIndex      = 0;

char        g_curBook[128]  = {};
uint16_t    g_curChapter    = 0;
uint16_t    g_curPage       = 0;
uint16_t    g_chapterCount  = 0;
uint16_t    g_pagesInChapter = 0;
uint16_t    g_totalPages    = 0;
uint32_t    g_totalWords    = 0;
char*       g_plainText     = nullptr;
size_t      g_plainTextLen  = 0;
LayoutPage* g_pages         = nullptr;
uint16_t    g_pageCount     = 0;

TocEntry    g_toc[64]       = {};
uint16_t    g_tocCount      = 0;
uint16_t    g_tocScroll     = 0;

char        g_jumpBuf[8]    = {};
uint8_t     g_jumpLen       = 0;

int32_t     g_touchBase     = 0;
int32_t     g_scrollAccum   = 0;
bool        g_needsRedraw   = false;
