#include <globals.h>
#include <ArduinoJson.h>
#include <pocketmage_oled.h>
#include <Fonts/FreeSerif9pt8b.h>
#include <Fonts/FreeSerifBold9pt8b.h>
#include <Fonts/FreeSerifItalic9pt8b.h>
#include <Fonts/FreeSerifBoldItalic9pt8b.h>
#include <Fonts/FreeSerifBold12pt8b.h>
#include <Fonts/FreeSerifBoldItalic12pt8b.h>
#include <Fonts/FreeSerifBold18pt8b.h>
#include <Fonts/FreeMonoBold9pt8b.h>
#include "reader.h"

// Display constants
static constexpr int16_t MARGIN_X = 8;
static constexpr int16_t MARGIN_Y = 24;
static constexpr int16_t USABLE_W = 304;
static constexpr int16_t USABLE_H = 200;
static constexpr int16_t PAGE_LINE_H = 13;

// Font map
struct FontSlot {
    const GFXfont* font;
    int8_t   baseline; // ascent - descent, used for leading
};

static FontSlot pick_font(uint8_t heading, uint8_t style, bool code) {
    if (code)                       return { &FreeMonoBold9pt8b, 12 };
    if (heading == 1)               return { &FreeSerifBold18pt8b, 22 };
    if (heading == 2)               return { &FreeSerifBold12pt8b, 17 };
    if (heading >= 3)               return { &FreeSerifBold9pt8b, 14 };
    if (style == 0)                 return { &FreeSerif9pt8b, 12 };
    if (style == 1)                 return { &FreeSerifBold9pt8b, 12 };
    if (style == 2)                 return { &FreeSerifItalic9pt8b, 12 };
    if (style >= 3)                 return { &FreeSerifBoldItalic9pt8b, 12 };
    return { &FreeSerif9pt8b, 12 };
}

// AST flattening (iterative, no recursion to save stack)
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

static void ast_flatten(JsonVariantConst root, AstCtx* ctx) {
    FlattenFrame stack[64];
    int sp = 0;
    stack[sp++] = { root, 0, 0, 0, 0 };

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
            for (size_t i = 0; i < len; i++) {
                if (content[i] != ' ' && content[i] != '\n' && content[i] != '\r' && content[i] != '\t') {
                    onlySpace = false; break;
                }
            }
            if (onlySpace) {
                if (ctx->poolUsed + 2 < ctx->poolCap) {
                    ctx->runs[ctx->runCount].text    = &ctx->pool[ctx->poolUsed];
                    ctx->runs[ctx->runCount].len     = 1;
                    ctx->runs[ctx->runCount].font    = style;
                    ctx->runs[ctx->runCount].heading = heading;
                    ctx->runs[ctx->runCount].flags   = flags;
                    ctx->runs[ctx->runCount].indent  = indent;
                    ctx->pool[ctx->poolUsed++] = ' ';
                    ctx->pool[ctx->poolUsed++] = '\0';
                    ctx->runCount++;
                }
                continue;
            }

            int copyLen = min((int)len, ctx->poolCap - ctx->poolUsed - 1);
            memcpy(&ctx->pool[ctx->poolUsed], content, copyLen);
            ctx->pool[ctx->poolUsed + copyLen] = '\0';

            ctx->runs[ctx->runCount].text    = &ctx->pool[ctx->poolUsed];
            ctx->runs[ctx->runCount].len     = copyLen;
            ctx->runs[ctx->runCount].font    = style;
            ctx->runs[ctx->runCount].heading = heading;
            ctx->runs[ctx->runCount].flags   = flags;
            ctx->runs[ctx->runCount].indent  = indent;
            ctx->runCount++;
            ctx->poolUsed += copyLen + 1;
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
            else if (strcmp(tag, "strong") == 0 || strcmp(tag, "b") == 0) s = 1;
            else if (strcmp(tag, "em") == 0 || strcmp(tag, "i") == 0)     s = (s == 1) ? 3 : 2;
            else if (strcmp(tag, "code") == 0 || strcmp(tag, "pre") == 0) f |= 1;
            else if (strcmp(tag, "blockquote") == 0)                       f |= 2;
            else if (strcmp(tag, "li") == 0)                               f |= 4, ind += 20;
            else if (strcmp(tag, "ul") == 0 || strcmp(tag, "ol") == 0)    ind += 10;
            else if (strcmp(tag, "br") == 0) {
                if (ctx->poolUsed + 2 < ctx->poolCap) {
                    ctx->runs[ctx->runCount].text    = &ctx->pool[ctx->poolUsed];
                    ctx->runs[ctx->runCount].len     = 1;
                    ctx->runs[ctx->runCount].font    = s;
                    ctx->runs[ctx->runCount].heading = h;
                    ctx->runs[ctx->runCount].flags   = f;
                    ctx->runs[ctx->runCount].indent  = ind;
                    ctx->pool[ctx->poolUsed++] = '\n';
                    ctx->pool[ctx->poolUsed++] = '\0';
                    ctx->runCount++;
                }
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

// Layout: runs -> lines -> pages
struct LineBuf {
    StyledRun runs[MAX_RUNS_PER_LINE];
    int       runCount;
    int16_t   height;
    int16_t   indent;
    int16_t   consumedX; // running pixel width
};

static LayoutPage s_pages[32];
static int s_pageCount;

static void layout_runs(StyledRun* runs, int runCount) {
    s_pageCount = 0;

    LineBuf line = {};
    int16_t yUsed = 0;

    auto flushLine = [&]() {
        if (line.runCount == 0) return;
        if (s_pageCount >= 32) return;
        LayoutPage* pg = &s_pages[s_pageCount];
        if (pg->lineCount >= MAX_LINES_PER_PAGE) return;

        LayoutLine ll;
        ll.runCount = line.runCount;
        ll.height   = (uint16_t)max(line.height, PAGE_LINE_H);
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

    for (int i = 0; i < runCount; i++) {
        StyledRun* run = &runs[i];

        // Newline is force flush
        if (run->text[0] == '\n') {
            flushLine();
            // Blank line after headings
            if (run->heading > 0 || (run->flags & 2)) {
                flushLine();
            }
            continue;
        }

        FontSlot fs = pick_font(run->heading, run->font, run->flags & 1);
        if (fs.baseline > line.height) line.height = fs.baseline;

        // Measure word
        display.setFont(fs.font);
        int16_t x1, y1; uint16_t w, h;
        String txt(run->text, run->len);
        display.getTextBounds(txt.c_str(), 0, 0, &x1, &y1, &w, &h);

        int16_t startX = MARGIN_X + run->indent;
        int16_t avail  = USABLE_W - run->indent;
        if (w > (uint16_t)avail && line.runCount > 0) {
            // Word wrap
            flushLine();
            if (run->indent > line.indent) line.indent = run->indent;
        } else if (line.runCount == 0) {
            line.indent = run->indent;
        }

        if (line.runCount < MAX_RUNS_PER_LINE) {
            line.runs[line.runCount] = *run;
            line.runCount++;
            line.consumedX += w;
        }
    }

    flushLine();
    if (s_pageCount < 32 && s_pages[s_pageCount].lineCount > 0) s_pageCount++;
}

// Public render API
void render_chapter(const char* json) {
    Serial.printf("[render_chapter] json len=%u\n", strlen(json));
    g_pagesInChapter = 0;
    g_pageCount = 0;

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, json);
    Serial.printf("[render_chapter] deserializeJson err=%s\n", err.c_str());
    if (err) { Serial.printf("[render_chapter] JSON PARSE FAILED\n"); return; }

    // Flatten AST (heap-allocated to save DRAM BSS)
    static StyledRun* s_runs = nullptr;
    static char*      s_pool = nullptr;
    if (s_runs) free(s_runs);
    if (s_pool) free(s_pool);
    s_runs = (StyledRun*)malloc(1024 * sizeof(StyledRun));
    s_pool = (char*)malloc(24576);
    if (!s_runs || !s_pool) { Serial.printf("[render_chapter] MALLOC FAILED for runs/pool\n"); g_pagesInChapter = 0; g_pageCount = 1; return; }
    Serial.printf("[render_chapter] malloc OK runs=%p pool=%p\n", s_runs, s_pool);
    AstCtx ctx = { s_runs, 1024, 0, s_pool, 24576, 0 };

    JsonVariant ast = doc["ast"];
    if (!ast.isNull()) {
        ast_flatten(ast, &ctx);
    } else if (doc["content"].is<const char*>()) {
        const char* text = doc["content"];
        size_t text_len = strlen(text);
        const char* p = text;
        const char* end = text + text_len;
        while (p < end && ctx.runCount < ctx.maxRuns) {
            const char* line_end = p;
            while (line_end < end && *line_end != '\n') line_end++;
            const char* trim = line_end;
            while (trim > p && (trim[-1] == ' ' || trim[-1] == '\t')) trim--;
            size_t line_len = trim - p;
            if (line_len > 0) {
                char* pool_ptr = ctx.pool + ctx.poolUsed;
                size_t room = ctx.poolCap - ctx.poolUsed;
                if (line_len > room) break;
                memcpy(pool_ptr, p, line_len);
                ctx.poolUsed += line_len;
                StyledRun* run = &ctx.runs[ctx.runCount];
                run->text = pool_ptr;
                run->len = line_len;
                run->font = 0;
                run->heading = 0;
                run->flags = 0;
                run->indent = 0;
                ctx.runCount++;
            }
            p = line_end + 1;
        }
    }

    Serial.printf("[render_chapter] running layout with %d runs\n", ctx.runCount);
    // Layout
    layout_runs(s_runs, ctx.runCount);

    g_pages     = s_pages;
    g_pageCount = (uint16_t)s_pageCount;
    g_pagesInChapter = g_pageCount;
    Serial.printf("[render_chapter] done pageCount=%u pagesInChapter=%u\n", g_pageCount, g_pagesInChapter);
    if (g_pageCount == 0) g_pageCount = 1;
}

void render_page_to_eink(uint16_t pageIdx) {
    display.fillScreen(GxEPD_WHITE);

    if (!g_pages || g_pageCount == 0) {
        display.setFont(&FreeSerif9pt8b);
        display.setCursor(10, 60);
        display.print("No content");
        return;
    }

    if (pageIdx >= g_pageCount) pageIdx = g_pageCount - 1;

    LayoutPage* pg = &g_pages[pageIdx];

    int16_t y = MARGIN_Y;
    for (uint8_t li = 0; li < pg->lineCount; li++) {
        LayoutLine* ll = &pg->lines[li];
        int16_t x = MARGIN_X + ll->indentLevel;

        for (uint8_t ri = 0; ri < ll->runCount; ri++) {
            StyledRun* run = &ll->runs[ri];
            if (run->len == 0) continue;

            FontSlot fs = pick_font(run->heading, run->font, run->flags & 1);
            display.setFont(fs.font);

            // Measure
            int16_t tx, ty; uint16_t tw, th;
            String txt(run->text, run->len);
            display.getTextBounds(txt.c_str(), 0, 0, &tx, &ty, &tw, &th);

            // Code block: background
            if (run->flags & 1) {
                display.fillRect(x - 2, y - fs.baseline + 2, tw + 4, fs.baseline + 2, GxEPD_BLACK);
                display.setTextColor(GxEPD_WHITE);
            } else {
                display.setTextColor(GxEPD_BLACK);
            }

            // Blockquote: left bar
            if ((run->flags & 2) && ri == 0) {
                display.fillRect(MARGIN_X + 2, y - fs.baseline + 2, 3, fs.baseline, GxEPD_BLACK);
            }

            // List bullet
            if ((run->flags & 4) && ri == 0) {
                display.fillCircle(x - 6, y - fs.baseline / 2 + 1, 2, GxEPD_BLACK);
            }

            // Heading underline
            if (run->heading > 0 && ri == ll->runCount - 1) {
                display.drawFastHLine(MARGIN_X, y + 2, USABLE_W, GxEPD_BLACK);
            }

            display.setCursor(x, y);
            display.print(txt);
            if (run->flags & 1) display.setTextColor(GxEPD_BLACK);

            x += tw;
        }

        y += ll->height;
    }

    // Footer line
    display.drawFastHLine(0, USABLE_H + MARGIN_Y + 2, display.width(), GxEPD_BLACK);
}

// OLED update
void updateOLED() {
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_5x8_tf);

    if (g_appMode == MODE_LIBRARY) {
        u8g2.drawStr(0, 8, "ePub Reader");
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
