#include <globals.h>
#include "reader.h"

static constexpr uint32_t SCROLL_COOLDOWN = 80;  // ms between scroll events
static uint32_t s_lastScrollMs = 0;

void touch_init() {
    TOUCH().setDynamicScroll(0);
    TOUCH().setPrevDynamicScroll(0);
    TOUCH().resetLastTouch();
    s_lastScrollMs = 0;
}

void touch_scroll() {
    // Only process if we're in reading mode with content
    if (g_appMode != MODE_READING || g_pagesInChapter == 0) return;
    
    uint32_t now = millis();
    if (now - s_lastScrollMs < SCROLL_COOLDOWN) return;

    // Get scroll vector: positive = up swipe, negative = down swipe
    int vector = TOUCH().getScrollVector();
    
    if (vector == 0) {
        // No significant movement, but reset baseline
        TOUCH().setDynamicScroll(0);
        TOUCH().setPrevDynamicScroll(0);
        return;
    }

    // vector > 0 = swiping UP (move to next page)
    // vector < 0 = swiping DOWN (move to previous page)
    if (vector > 0) {
        if (g_curPage + 1 < g_pagesInChapter) {
            g_curPage++;
            g_needsRedraw = true;
            s_lastScrollMs = now;
        }
    } else if (vector < 0) {
        if (g_curPage > 0) {
            g_curPage--;
            g_needsRedraw = true;
            s_lastScrollMs = now;
        }
    }

    // Reset touch state after processing
    TOUCH().setDynamicScroll(0);
    TOUCH().setPrevDynamicScroll(0);
    TOUCH().resetLastTouch();
}
