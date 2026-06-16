#include <globals.h>
#include <ArduinoJson.h>
#include <pocketmage_oled.h>
#include <U8g2_for_Adafruit_GFX.h>
#include "reader.h"

extern int md4c_to_runs(const char* text, size_t len, StyledRun* runs, int max_runs, char* pool, int pool_cap);
extern int html_to_runs(const char* text, size_t len, StyledRun* runs, int max_runs, char* pool, int pool_cap);

static U8G2_FOR_ADAFRUIT_GFX u8g2Fonts;

// CONSTANTS
static constexpr int16_t MARGIN_X = 8;
static constexpr int16_t MARGIN_Y = 24;
static constexpr int16_t USABLE_W = 304;
static constexpr int16_t USABLE_H = 200;
static constexpr int16_t PAGE_LINE_H = 13;
static constexpr int MAX_PAGES = 32;
static constexpr int MAX_RUNS_TOTAL = 2048;
static constexpr int POOL_SIZE = 32768;

// FONT PICKER
struct FontSlot {
    const uint8_t* font;
    int8_t baseline;
};

static FontSlot pick_font(uint8_t heading, uint8_t style, bool code) {
    if (code)                       return { u8g2_font_courB12_tf, 10 };
    if (heading == 1)               return { u8g2_font_ncenB24_tf, 18 };
    if (heading == 2)               return { u8g2_font_ncenB18_tf, 14 };
    if (heading >= 3)               return { u8g2_font_ncenB14_tf, 12 };
    if (style == 0)                 return { u8g2_font_ncenR12_tf, 10 };
    if (style == 1)                 return { u8g2_font_ncenB12_tf, 10 };
    if (style == 2)                 return { u8g2_font_ncenR12_tf, 10 };
    if (style >= 3)                 return { u8g2_font_ncenB12_tf, 10 };
    return { u8g2_font_ncenR12_tf, 10 };
}

// AST FLATTENING
struct AstCtx {
    StyledRun* runs;
    int        maxRuns;
    int        runCount;
    char*      pool;
    int        poolCap;
    int        poolUsed;
};

struct FlattenFrame {
    JsonVariantConst node;
    uint8_t   heading;
    uint8_t   style;
    uint8_t   flags;
    uint16_t  indent;
};

// Helper: try to merge text into the last run if possible
static bool try_merge_text(AstCtx* ctx, const char* text, size_t len, uint8_t heading, uint8_t style, uint8_t flags, uint16_t indent) {
    if (ctx->runCount == 0) return false;
    StyledRun* last = &ctx->runs[ctx->runCount - 1];
    
    // Never merge across a newline (from <br>)
    if (last->text[0] == '\n') return false;

    // Only merge if attributes match exactly
    if (last->heading != heading || last->font != style || 
        last->flags != flags || last->indent != indent) {
        return false;
    }
    
    // Check if we have room in the pool
    size_t newLen = last->len + len + 1; // +1 for space separator
    if (ctx->poolUsed + newLen + 1 > ctx->poolCap) return false;
    
    // Move the existing text to the end of the pool (simpler than trying to append in place)
    const char* oldText = last->text;
    size_t oldLen = last->len;
    
    // We'll rebuild the run at the end of the pool
    char* dest = ctx->pool + ctx->poolUsed;
    
    // Copy old text
    memcpy(dest, oldText, oldLen);
    dest[oldLen] = ' '; // Space between words
    memcpy(dest + oldLen + 1, text, len);
    dest[oldLen + 1 + len] = '\0';
    
    // Update the run
    last->text = dest;
    last->len = oldLen + 1 + len;
    ctx->poolUsed += oldLen + 1 + len + 1;
    
    return true;
}

static void ast_flatten(JsonVariantConst root, AstCtx* ctx) {
    FlattenFrame stack[64];
    int sp = 0;
    stack[sp++] = { root, 0, 0, 0, 0 };

    bool forceNewline = false;

    while (sp > 0) {
        FlattenFrame f = stack[--sp];
        JsonVariantConst node = f.node;
        uint8_t  heading = f.heading;
        uint8_t  style   = f.style;
        uint8_t  flags   = f.flags;
        uint16_t indent  = f.indent;

        if (ctx->runCount >= ctx->maxRuns) break;
        if (ctx->poolUsed >= ctx->poolCap) break;

        const char* type = node["type"];

        if (strcmp(type, "Text") == 0) {
            const char* content = node["content"];
            if (!content || !*content) continue;
            size_t len = strlen(content);

            bool onlySpace = true;
            bool hasNewline = false;
            for (size_t i = 0; i < len; i++) {
                if (content[i] == '\n') { hasNewline = true; }
                if (content[i] != ' ' && content[i] != '\n' && content[i] != '\r' && content[i] != '\t') {
                    onlySpace = false;
                    break;
                }
            }

            if (onlySpace) {
                if (hasNewline) {
                    forceNewline = true;
                }
                continue;
            }

            if (forceNewline) {
                if (ctx->poolUsed + 2 < ctx->poolCap) {
                    ctx->runs[ctx->runCount].text = ctx->pool + ctx->poolUsed;
                    ctx->runs[ctx->runCount].len = 1;
                    ctx->runs[ctx->runCount].font = style;
                    ctx->runs[ctx->runCount].heading = heading;
                    ctx->runs[ctx->runCount].flags = flags;
                    ctx->runs[ctx->runCount].indent = indent;
                    ctx->pool[ctx->poolUsed++] = '\n';
                    ctx->pool[ctx->poolUsed++] = '\0';
                    ctx->runCount++;
                }
                forceNewline = false;
            }

            if (ctx->runCount > 0) {
                StyledRun* last = &ctx->runs[ctx->runCount - 1];
                if (last->len == 1 && last->text[0] == '\n') {
                    // Don't merge with newline - start fresh
                } else if (try_merge_text(ctx, content, len, heading, style, flags, indent)) {
                    continue;
                }
            }

            if (ctx->poolUsed + (int)len + 1 >= ctx->poolCap) break;

            char* dest = ctx->pool + ctx->poolUsed;
            memcpy(dest, content, len);
            dest[len] = '\0';

            ctx->runs[ctx->runCount].text = dest;
            ctx->runs[ctx->runCount].len = len;
            ctx->runs[ctx->runCount].font = style;
            ctx->runs[ctx->runCount].heading = heading;
            ctx->runs[ctx->runCount].flags = flags;
            ctx->runs[ctx->runCount].indent = indent;
            ctx->runCount++;
            ctx->poolUsed += len + 1;
            continue;
        }

        if (strcmp(type, "Comment") == 0) continue;

        if (strcmp(type, "Element") == 0) {
            const char* tag = node["tag"];
            uint8_t  h = heading;
            uint8_t  s = style;
            uint8_t  f = flags;
            uint16_t ind = indent;

            if      (strcmp(tag, "h1") == 0)  h = 1;
            else if (strcmp(tag, "h2") == 0)  h = 2;
            else if (strcmp(tag, "h3") == 0)  h = 3;
            else if (strcmp(tag, "h4") == 0)  h = 4;
            else if (strcmp(tag, "h5") == 0)  h = 5;
            else if (strcmp(tag, "h6") == 0)  h = 6;
            else if (strcmp(tag, "strong") == 0 || strcmp(tag, "b") == 0) {
                s = (s == 2) ? 3 : 1;
            }
            else if (strcmp(tag, "em") == 0 || strcmp(tag, "i") == 0) {
                s = (s == 1) ? 3 : 2;
            }
            else if (strcmp(tag, "code") == 0 || strcmp(tag, "pre") == 0) f |= 1;
            else if (strcmp(tag, "blockquote") == 0) f |= 2;
            else if (strcmp(tag, "li") == 0) { f |= 4; ind += 20; }
            else if (strcmp(tag, "ul") == 0 || strcmp(tag, "ol") == 0) ind += 10;
            else if (strcmp(tag, "br") == 0) {
                forceNewline = true;
                continue;
            }

            JsonArrayConst children = node["children"];
            size_t numChildren = children.size();
            if (numChildren == 0) continue;
            if (sp + (int)numChildren > 64) numChildren = 64 - sp;

            for (int ci = (int)numChildren - 1; ci >= 0; ci--) {
                stack[sp++] = { children[(size_t)ci], h, s, f, ind };
            }
        }
    }
}

// LAYOUT
struct LineBuf {
    StyledRun runs[MAX_RUNS_PER_LINE];
    int       runCount;
    int16_t   height;
    int16_t   indent;
    int16_t   consumedX;
};

static LayoutPage s_pages[MAX_PAGES];
static int s_pageCount;

// Walk split backward if it lands in the middle of a multi-byte UTF-8 sequence.
// Returns the adjusted split (exclusive byte count) that falls on a char boundary.
static int utf8_align_split(const char* text, int split) {
    if (split <= 0) return 0;
    // If the byte right after the prefix is a continuation byte, the
    // character that owns it started before split but isn't fully included.
    // Walk back to that character's start and exclude it entirely.
    if (((unsigned char)text[split] & 0xC0) == 0x80) {
        int start = split;
        while (start > 0 && ((unsigned char)text[start] & 0xC0) == 0x80)
            start--;
        return start;
    }
    // If the last byte of the prefix is a lead byte whose continuation
    // extends past split, exclude it too.
    unsigned char c = (unsigned char)text[split - 1];
    int seq_len = 1;
    if      ((c & 0xE0) == 0xC0) seq_len = 2;
    else if ((c & 0xF0) == 0xE0) seq_len = 3;
    else if ((c & 0xF8) == 0xF0) seq_len = 4;
    if (seq_len > 1)
        return split - 1;
    return split;
}

// Find word-break index within a run: longest prefix that fits availWidth
// Returns the index of the first character of the overflow (the split point)
static int find_word_break(const StyledRun* run, int16_t availWidth, FontSlot fs, int16_t* outPrefixWidth) {
    u8g2Fonts.setFont(fs.font);

    int lo = 0, hi = min((int)run->len, 1023);
    int lastSpace = -1;

    char buf[1024];
    while (lo < hi) {
        int mid = (lo + hi + 1) / 2;
        memcpy(buf, run->text, mid);
        buf[mid] = '\0';
        int16_t pw = u8g2Fonts.getUTF8Width(buf);
        if (pw <= availWidth) {
            lo = mid;
        } else {
            hi = mid - 1;
        }
    }
    lo = utf8_align_split(run->text, lo);

    for (int ci = 0; ci < lo; ci++) {
        if (run->text[ci] == ' ') lastSpace = ci + 1;
    }

    int split;
    if (lastSpace > 0) {
        split = lastSpace;
    } else if (lo > 0) {
        split = lo;
    } else {
        if (outPrefixWidth) *outPrefixWidth = 0;
        return 0;
    }

    while (split > 0 && run->text[split - 1] == ' ') split--;

    if (outPrefixWidth && split > 0) {
        memcpy(buf, run->text, split);
        buf[split] = '\0';
        *outPrefixWidth = u8g2Fonts.getUTF8Width(buf);
    } else if (outPrefixWidth) {
        *outPrefixWidth = 0;
    }

    return split;
}

static void layout_runs(StyledRun* runs, int runCount) {
    s_pageCount = 0;

    LineBuf line = {};
    int16_t yUsed = 0;

    auto flushLine = [&]() {
        if (line.runCount == 0) return;
        if (s_pageCount >= MAX_PAGES) return;
        LayoutPage* pg = &s_pages[s_pageCount];
        if (pg->lineCount >= MAX_LINES_PER_PAGE) {
            s_pageCount++;
            pg = &s_pages[s_pageCount];
            pg->lineCount = 0;
            yUsed = 0;
        }

        LayoutLine ll;
        ll.runCount = line.runCount;
        ll.height = (uint16_t)max(line.height, PAGE_LINE_H);
        ll.indentLevel = (uint8_t)line.indent;
        memcpy(ll.runs, line.runs, line.runCount * sizeof(StyledRun));
        pg->lines[pg->lineCount++] = ll;

        yUsed += ll.height;
        if (yUsed + PAGE_LINE_H > USABLE_H) {
            s_pageCount++;
            yUsed = 0;
        }

        line = {};
    };

    StyledRun deferred = {};
    bool hasDeferred = false;

    int i = 0;
    while (i < runCount || hasDeferred) {
        StyledRun* run;
        if (hasDeferred) {
            run = &deferred;
            hasDeferred = false;
        } else {
            run = &runs[i];
            i++;
        }

        // Newline: force flush and add blank line if needed
        if (run->text[0] == '\n') {
            flushLine();
            if (run->heading > 0 || (run->flags & 2)) {
                if (s_pageCount < MAX_PAGES) {
                    LayoutPage* pg = &s_pages[s_pageCount];
                    if (pg->lineCount < MAX_LINES_PER_PAGE) {
                        LayoutLine empty = {};
                        empty.runCount = 0;
                        empty.height = PAGE_LINE_H;
                        empty.indentLevel = 0;
                        pg->lines[pg->lineCount++] = empty;
                        yUsed += PAGE_LINE_H;
                        if (yUsed + PAGE_LINE_H > USABLE_H) {
                            s_pageCount++;
                            yUsed = 0;
                        }
                    }
                }
            }
            continue;
        }

        FontSlot fs = pick_font(run->heading, run->font, run->flags & 1);
        if (fs.baseline > line.height) line.height = fs.baseline;

        u8g2Fonts.setFont(fs.font);
        int runlen = min((int)run->len, 1023);
        char buf[1024];
        memcpy(buf, run->text, runlen);
        buf[runlen] = '\0';
        int16_t w = u8g2Fonts.getUTF8Width(buf);

        int16_t avail = USABLE_W - run->indent;
        int16_t remaining = avail - line.consumedX;

        // Check if we need to wrap (run doesn't fit)
        if (w > remaining) {
            if (line.runCount > 0) {
                flushLine();
                remaining = avail;
            }

            if (w > remaining) {
                int16_t prefixW;
                int split = find_word_break(run, remaining, fs, &prefixW);
                if (split > 0 && split < (int)run->len) {
                    // Emit the prefix on the current line
                    if (line.runCount < MAX_RUNS_PER_LINE) {
                        line.runs[line.runCount] = *run;
                        line.runs[line.runCount].len = split;
                        line.runCount++;
                        line.consumedX += prefixW;
                        line.indent = run->indent;
                    }

                    // Skip trailing spaces that were trimmed
                    const char* overflowText = run->text + split;
                    int overflowLen = run->len - split;
                    while (overflowLen > 0 && *overflowText == ' ') {
                        overflowText++;
                        overflowLen--;
                    }

                    if (overflowLen > 0) {
                        // Hold overflow for next iteration
                        deferred = *run;
                        deferred.text = overflowText;
                        deferred.len = overflowLen;
                        hasDeferred = true;
                    }

                    // Emit the prefix line
                    flushLine();
                    continue;
                }
                // If no split possible (single char wider than screen), let it overflow
            }

            // Set indent for this line
            line.indent = run->indent;
        } else if (line.runCount == 0) {
            line.indent = run->indent;
            line.consumedX = 0;
        }

        // Add the run to the current line
        if (line.runCount < MAX_RUNS_PER_LINE) {
            line.runs[line.runCount] = *run;
            line.runCount++;
            line.consumedX += w;
        } else {
            // Too many runs per line, flush and start fresh
            flushLine();
            line.indent = run->indent;
            line.consumedX = 0;
            if (line.runCount < MAX_RUNS_PER_LINE) {
                line.runs[line.runCount] = *run;
                line.runCount++;
                line.consumedX += w;
            }
        }
    }

    flushLine();

    int pageCount = s_pageCount;
    if (pageCount < MAX_PAGES && s_pages[pageCount].lineCount > 0) {
        pageCount++;
    }
    s_pageCount = pageCount;

    if (s_pageCount == 0) {
        s_pageCount = 1;
        s_pages[0].lineCount = 0;
    }
}

// PUBLIC RENDER API
void render_chapter_markup(const char* text, size_t len, uint8_t markup) {
    Serial.printf("[render_chapter_markup] len=%zu markup=%u\n", len, markup);
    g_pagesInChapter = 0;
    g_pageCount = 0;

    // Static pointers to free on each call
    static StyledRun* s_runs = nullptr;
    static char*      s_pool = nullptr;
    
    // Free previous allocations
    if (s_runs) { free(s_runs); s_runs = nullptr; }
    if (s_pool) { free(s_pool); s_pool = nullptr; }

    // Allocate
    s_runs = (StyledRun*)malloc(MAX_RUNS_TOTAL * sizeof(StyledRun));
    s_pool = (char*)malloc(POOL_SIZE);
    
    if (!s_runs || !s_pool) {
        Serial.printf("[render_chapter_markup] MALLOC FAILED\n");
        if (s_runs) { free(s_runs); s_runs = nullptr; }
        if (s_pool) { free(s_pool); s_pool = nullptr; }
        g_pagesInChapter = 0;
        g_pageCount = 1;
        return;
    }

    int runCount;
    if (markup == 1) {
        runCount = html_to_runs(text, len, s_runs, MAX_RUNS_TOTAL, s_pool, POOL_SIZE);
    } else {
        runCount = md4c_to_runs(text, len, s_runs, MAX_RUNS_TOTAL, s_pool, POOL_SIZE);
    }

    Serial.printf("[render_chapter_markup] parse returned %d runs\n", runCount);

    if (runCount <= 0) {
        // Parse failed, create a fallback "error" page
        free(s_runs); s_runs = nullptr;
        free(s_pool); s_pool = nullptr;
        g_pagesInChapter = 0;
        g_pageCount = 1;
        return;
    }

    layout_runs(s_runs, runCount);

    g_pages = s_pages;
    g_pageCount = (uint16_t)s_pageCount;
    g_pagesInChapter = g_pageCount;
    
    // Safety: at least one page
    if (g_pageCount == 0) {
        g_pageCount = 1;
        g_pagesInChapter = 1;
    }
    
    Serial.printf("[render_chapter_markup] done pages=%u\n", g_pageCount);
}

// PAGE RENDERER
void render_page_to_eink(uint16_t pageIdx) {
    u8g2Fonts.begin(display);
    u8g2Fonts.setFontMode(1);
    display.fillScreen(GxEPD_WHITE);

    if (!g_pages || g_pageCount == 0) {
        u8g2Fonts.setFont(u8g2_font_ncenR12_tf);
        u8g2Fonts.setForegroundColor(GxEPD_BLACK);
        u8g2Fonts.drawUTF8(10, 60, "No content");
        return;
    }

    if (pageIdx >= g_pageCount) pageIdx = g_pageCount - 1;

    LayoutPage* pg = &g_pages[pageIdx];
    int16_t y = MARGIN_Y;

    for (uint8_t li = 0; li < pg->lineCount; li++) {
        LayoutLine* ll = &pg->lines[li];

        if (ll->runCount == 0) {
            y += ll->height;
            continue;
        }

        int16_t x = MARGIN_X + ll->indentLevel;

        for (uint8_t ri = 0; ri < ll->runCount; ri++) {
            StyledRun* run = &ll->runs[ri];
            if (run->len == 0) continue;

            FontSlot fs = pick_font(run->heading, run->font, run->flags & 1);
            u8g2Fonts.setFont(fs.font);

            int runlen = min((int)run->len, 1023);
            char buf[1024];
            memcpy(buf, run->text, runlen);
            buf[runlen] = '\0';
            int16_t tw = u8g2Fonts.getUTF8Width(buf);

            int rightEdge = MARGIN_X + USABLE_W;

            if (run->flags & 1) {
                int bgX = x - 2;
                int bgW = tw + 4;
                if (bgX + bgW > rightEdge) {
                    bgW = rightEdge - bgX;
                }
                if (bgW > 0) {
                    display.fillRect(bgX, y - fs.baseline + 2, bgW, fs.baseline + 2, GxEPD_BLACK);
                    u8g2Fonts.setForegroundColor(GxEPD_WHITE);
                } else {
                    u8g2Fonts.setForegroundColor(GxEPD_BLACK);
                }
            } else {
                u8g2Fonts.setForegroundColor(GxEPD_BLACK);
            }

            if ((run->flags & 2) && ri == 0) {
                display.fillRect(MARGIN_X + 2, y - fs.baseline + 2, 3, fs.baseline, GxEPD_BLACK);
                if (x < MARGIN_X + 12) x = MARGIN_X + 12;
            }

            if ((run->flags & 4) && ri == 0) {
                display.fillCircle(x - 6, y - fs.baseline / 2 + 1, 2, GxEPD_BLACK);
            }

            if (run->heading > 0 && ri == ll->runCount - 1) {
                display.drawFastHLine(MARGIN_X, y + 2, USABLE_W, GxEPD_BLACK);
            }

            if (x + tw > rightEdge) {
                int16_t clipW = rightEdge - x;
                if (clipW > 0) {
                    char clipped[256];
                    int lo = 0, hi = min((int)run->len, 255);
                    while (lo < hi) {
                        int mid = (lo + hi + 1) / 2;
                        memcpy(clipped, run->text, mid);
                        clipped[mid] = '\0';
                        int16_t cw = u8g2Fonts.getUTF8Width(clipped);
                        if (cw <= clipW) lo = mid;
                        else hi = mid - 1;
                    }
                    lo = utf8_align_split(run->text, lo);
                    if (lo > 0) {
                        memcpy(clipped, run->text, lo);
                        clipped[lo] = '\0';
                        x = u8g2Fonts.drawUTF8(x, y, clipped);
                    }
                    if (run->flags & 1) u8g2Fonts.setForegroundColor(GxEPD_BLACK);
                    continue;
                }
            }

            x = u8g2Fonts.drawUTF8(x, y, buf);
            if (run->flags & 1) u8g2Fonts.setForegroundColor(GxEPD_BLACK);
        }

        y += ll->height;
    }

    display.drawFastHLine(0, USABLE_H + MARGIN_Y + 2, display.width(), GxEPD_BLACK);
}

// OLED update
void updateOLED() {
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_5x8_tf);

    if (g_appMode == MODE_LIBRARY) {
        u8g2.drawStr(0, 8, "Honzo Reader");
        if (g_bookCount > 0 && g_selIndex < g_bookCount) {
            u8g2.setCursor(0, 20);
            u8g2.printf("%d/%d", g_selIndex + 1, g_bookCount);
        }
    } else if (g_appMode == MODE_READING) {
        u8g2.setCursor(0, 8);
        u8g2.printf("Ch %d/%d", g_curChapter + 1, g_chapterCount);
        if (g_pagesInChapter > 0) {
            u8g2.setCursor(0, 20);
            u8g2.printf("p %d/%d", g_curPage + 1, g_pagesInChapter);
        }
    } else if (g_appMode == MODE_HELP) {
        u8g2.drawStr(0, 8, "Help");
    } else if (g_appMode == MODE_TOC) {
        u8g2.drawStr(0, 8, "Table of Contents");
    } else if (g_appMode == MODE_JUMP) {
        u8g2.drawStr(0, 8, "Jump to page");
        u8g2.setCursor(0, 20);
        u8g2.print(g_jumpBuf);
    }

    u8g2.sendBuffer();
}
