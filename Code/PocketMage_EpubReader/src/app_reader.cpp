#include <globals.h>
#include <SD_MMC.h>
#include <Fonts/FreeSerif9pt8b.h>
#include "reader.h"

#include "EpubExtractor.h"
using namespace capi;

static EpubExtractor* s_epub = nullptr;
static char*          s_chapterJson = nullptr;

// TOC JSON parser helpers
static const char* skip_ws(const char* p) {
    while (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t') p++;
    return p;
}

static const char* scan_string_val(const char* p, char* out, int maxLen) {
    if (*p != '"') return p;
    p++;
    int i = 0;
    while (*p && *p != '"' && i < maxLen - 1) {
        if (*p == '\\' && *(p+1)) { p++; }
        out[i++] = *p;
        p++;
    }
    out[i] = '\0';
    if (*p == '"') p++;
    return p;
}

static const char* scan_int_val(const char* p, int* out) {
    *out = 0;
    bool neg = false;
    p = skip_ws(p);
    if (*p == '-') { neg = true; p++; }
    while (*p >= '0' && *p <= '9') {
        *out = *out * 10 + (*p - '0');
        p++;
    }
    if (neg) *out = -*out;
    return p;
}

// Open book
void open_book(const char* filename) {
    Serial.printf("[open_book] filename=\"%s\"\n", filename);
    if (s_epub) { EpubExtractor_destroy(s_epub); s_epub = nullptr; Serial.printf("[open_book] destroyed old extractor\n"); }

    char full[256];
    snprintf(full, sizeof(full), "/sdcard/books/%s", filename);
    Serial.printf("[open_book] full path=\"%s\" len=%u\n", full, strlen(full));

    char sdcheck[256];
    snprintf(sdcheck, sizeof(sdcheck), "/books/%s", filename);
    Serial.printf("[open_book] SD_MMC.exists(\"%s\")=%d\n", sdcheck, (int)SD_MMC.exists(sdcheck));

    g_appMode = MODE_READING;

    s_epub = EpubExtractor_create(full, strlen(full));
    Serial.printf("[open_book] EpubExtractor_create returned %p\n", s_epub);
    if (!s_epub) {
        Serial.printf("[open_book] CREATE FAILED - null pointer\n");
        g_chapterCount = 0;
        g_needsRedraw = true;
        return;
    }

    bool valid = EpubExtractor_get_metadata_is_valid(s_epub);
    Serial.printf("[open_book] metadata_valid=%d\n", (int)valid);

    g_chapterCount = (uint16_t)EpubExtractor_get_chapter_count(s_epub);
    Serial.printf("[open_book] chapter_count=%u\n", g_chapterCount);
    // g_totalWords   = (uint32_t)EpubExtractor_get_total_word_count(s_epub);
    // Serial.printf("[open_book] total_words=%u\n", g_totalWords);
    g_totalWords = 0;
    g_totalPages   = 0;

    // Extract TOC from OPF metadata
    {
        char tocBuf[8192];
        DiplomatWriteable tocW = diplomat_simple_write(tocBuf, sizeof(tocBuf));
        auto tocResult = EpubExtractor_get_toc_json(s_epub, &tocW);
        if (tocResult.is_ok) {
            g_tocCount = 0;
            const char* p = tocBuf;
            while (*p && *p != '[') p++;
            if (*p == '[') {
                p++;
                while (*p && g_tocCount < 64) {
                    p = skip_ws(p);
                    if (*p == ']') break;
                    if (*p != '{') { p++; continue; }

                    int ci = -1;
                    char title[64] = {};
                    p++;
                    while (*p && *p != '}') {
                        p = skip_ws(p);
                        if (*p != '"') { p++; continue; }

                        char key[32] = {};
                        p = scan_string_val(p, key, sizeof(key));
                        p = skip_ws(p);
                        if (*p == ':') p++;
                        p = skip_ws(p);

                        if (strcmp(key, "chapter_index") == 0) {
                            p = scan_int_val(p, &ci);
                        } else if (strcmp(key, "title") == 0) {
                            p = scan_string_val(p, title, sizeof(title));
                        } else {
                            if (*p == '"') {
                                char dummy[8];
                                p = scan_string_val(p, dummy, sizeof(dummy));
                            } else {
                                while (*p && *p != ',' && *p != '}') p++;
                            }
                        }
                        p = skip_ws(p);
                        if (*p == ',') p++;
                    }
                    if (*p == '}') p++;

                    if (ci >= 0) {
                        g_toc[g_tocCount].index = (uint16_t)ci;
                        strncpy(g_toc[g_tocCount].title, title, sizeof(g_toc[0].title) - 1);
                        g_tocCount++;
                    }
                    p = skip_ws(p);
                    if (*p == ',') p++;
                }
            }
        } else {
            g_tocCount = 0;
        }
    }

    Serial.printf("[open_book] toc_count=%u\n", g_tocCount);

    // Restore bookmark
    Bookmark bm;
    if (loadBookmark(&bm)) {
        Serial.printf("[open_book] bookmark restored chapter=%u page=%u\n", bm.chapter, bm.page);
        g_curChapter = bm.chapter;
        g_curPage    = bm.page;
    } else {
        Serial.printf("[open_book] no bookmark, starting at 0,0\n");
        g_curChapter = 0;
        g_curPage    = 0;
    }

    Serial.printf("[open_book] calling reader_load_chapter(%u)\n", g_curChapter);
    reader_load_chapter(g_curChapter);
    Serial.printf("[open_book] done, setting g_needsRedraw\n");
    g_needsRedraw = true;
}

    // Load a chapter
void reader_load_chapter(uint16_t chapter) {
    Serial.printf("[load_chapter] chapter=%u g_chapterCount=%u s_epub=%p\n", chapter, g_chapterCount, s_epub);
    if (chapter >= g_chapterCount) {
        chapter = g_chapterCount > 0 ? g_chapterCount - 1 : 0;
        Serial.printf("[load_chapter] clamped to %u\n", chapter);
    }
    if (!s_epub) { Serial.printf("[load_chapter] NO EPUB - returning\n"); return; }

    g_curChapter = chapter;

    // Allocate chapter JSON buffer on first use (heap)
    if (!s_chapterJson) {
        Serial.printf("[load_chapter] allocating 32768 for chapterJson\n");
        s_chapterJson = (char*)malloc(32768);
        if (!s_chapterJson) { Serial.printf("[load_chapter] MALLOC FAILED\n"); return; }
        Serial.printf("[load_chapter] malloc OK ptr=%p\n", s_chapterJson);
    }

    DiplomatWriteable cw = diplomat_simple_write(s_chapterJson, 32767);
    Serial.printf("[load_chapter] calling get_single_chapter_json(%u)...\n", chapter);
    auto result = EpubExtractor_get_single_chapter_json(s_epub, chapter, &cw);
    Serial.printf("[load_chapter] get_single_chapter_json done is_ok=%d len=%u\n", (int)result.is_ok, cw.len);
    if (!result.is_ok) { Serial.printf("[load_chapter] GET SINGLE CHAPTER JSON FAILED\n"); return; }

    s_chapterJson[cw.len] = '\0';
    Serial.printf("[load_chapter] calling render_chapter (json_len=%u)\n", cw.len);
    render_chapter(s_chapterJson);
    Serial.printf("[load_chapter] render_chapter done g_pagesInChapter=%u\n", g_pagesInChapter);

    if (g_curPage >= g_pagesInChapter) g_curPage = 0;
    Serial.printf("[load_chapter] complete\n");
}

// Process keys
void reader_process_key(char ch) {
    if (ch == 21) {  // RIGHT -> next page
        if (g_curPage + 1 < g_pagesInChapter) {
            g_curPage++;
            g_needsRedraw = true;
        } else if (g_curChapter + 1 < g_chapterCount) {
            g_curChapter++;
            g_curPage = 0;
            reader_load_chapter(g_curChapter);
            saveBookmark();
            g_needsRedraw = true;
        }
    } else if (ch == 19) {  // LEFT -> prev page
        if (g_curPage > 0) {
            g_curPage--;
            g_needsRedraw = true;
        } else if (g_curChapter > 0) {
            g_curChapter--;
            reader_load_chapter(g_curChapter);
            g_curPage = g_pagesInChapter > 0 ? g_pagesInChapter - 1 : 0;
            saveBookmark();
            g_needsRedraw = true;
        }
    } else if (ch == 24) {  // UP -> prev chapter
        if (g_curChapter > 0) {
            g_curChapter--;
            g_curPage = 0;
            reader_load_chapter(g_curChapter);
            saveBookmark();
            g_needsRedraw = true;
        }
    } else if (ch == 25) {  // DOWN -> next chapter
        if (g_curChapter + 1 < g_chapterCount) {
            g_curChapter++;
            g_curPage = 0;
            reader_load_chapter(g_curChapter);
            saveBookmark();
            g_needsRedraw = true;
        }
    } else if (ch == 'b') {
        saveBookmark();
    } else if (ch == 't') {
        g_tocScroll = 0;
        g_appMode = MODE_TOC;
        g_needsRedraw = true;
    } else if (ch == 'h') {
        saveBookmark();
        SD_MMC.remove(CUR_PATH);
        if (s_epub) { EpubExtractor_destroy(s_epub); s_epub = nullptr; }
        library_init();
    } else if (ch == 'j') {
        g_jumpLen = 0;
        g_jumpBuf[0] = '\0';
        g_appMode = MODE_JUMP;
    } else if (ch == '?') {
        g_appMode = MODE_HELP;
        g_needsRedraw = true;
    }
}

// Reader render
void reader_render() {
    Serial.printf("[reader_render] s_epub=%p g_chapterCount=%u\n", s_epub, g_chapterCount);

    if (!s_epub || g_chapterCount == 0) {
        Serial.printf("[reader_render] CANNOT OPEN BOOK s_epub=%p chCount=%u\n", s_epub, g_chapterCount);
        display.fillScreen(GxEPD_WHITE);
        display.setFont(&FreeSerif9pt8b);
        display.setCursor(10, 60);
        display.print("Cannot open book");
        EINK().refresh();
        return;
    }

    render_page_to_eink(g_curPage);

    // Header overlay
    display.setFont(&Font5x7Fixed);
    display.setTextColor(GxEPD_BLACK);
    display.setCursor(8, 11);

    const char* title = "";
    for (uint16_t i = 0; i < g_tocCount; i++) {
        if (g_toc[i].index == g_curChapter) {
            title = g_toc[i].title;
            break;
        }
    }
    String hdr = title;
    if ((int)hdr.length() > 42) hdr = hdr.substring(0, 40) + "~";
    display.print(hdr);

    // Clock
    if (SYSTEM_CLOCK) {
        DateTime now = CLOCK().nowDT();
        char timeBuf[8];
        snprintf(timeBuf, sizeof(timeBuf), "%d:%02d", now.hour(), now.minute());
        int16_t tx1, ty1; uint16_t tw, th;
        display.getTextBounds(timeBuf, 0, 0, &tx1, &ty1, &tw, &th);
        display.setCursor(display.width() - (int)tw - 4, 11);
        display.print(timeBuf);
    }

    // Progress bar
    int py = display.height() - 3;
    int progW = display.width() - 16;
    display.drawRect(8, py - 6, progW, 5, GxEPD_BLACK);
    if (g_chapterCount > 0) {
        uint32_t globalPageNum = g_curPage;
        // Estimate total: each chapter roughly equal size
        uint32_t estTotal = g_pagesInChapter * g_chapterCount;
        if (estTotal > 0) {
            int filled = (int)((progW * (int64_t)globalPageNum) / estTotal);
            if (filled > progW) filled = progW;
            if (filled > 0) display.fillRect(8 + 1, py - 5, filled, 3, GxEPD_BLACK);
        }
    }

    // Bottom info
    display.setFont(&Font5x7Fixed);
    display.setTextColor(GxEPD_BLACK);
    display.setCursor(8, display.height() - 14);
    display.printf("Ch %d/%d", g_curChapter + 1, g_chapterCount);
    if (g_pagesInChapter > 0) {
        display.setCursor(display.width() - 68, display.height() - 14);
        display.printf("p %d/%d", g_curPage + 1, g_pagesInChapter);
    }

    EINK().refresh();
}
