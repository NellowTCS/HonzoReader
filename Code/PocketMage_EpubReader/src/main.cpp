#include <globals.h>
#include <pocketmage_oled.h>
#include <SD_MMC.h>
#include "reader.h"

#if OTA_APP

static void appTask(void*) {
    for (;;) {
        processKB_APP();
        vTaskDelay(50 / portTICK_PERIOD_MS);
        yield();
    }
}

void setup() {
    PocketMage_INIT();
    xTaskCreatePinnedToCore(appTask, "appTask", 32768, NULL, 2, NULL, 0);
    APP_INIT();
}

void loop() {
    vTaskDelay(pdMS_TO_TICKS(100));
    yield();
}

// E-ink handler task (created by setupEink in PocketMage library)
void einkHandler(void*) {
    vTaskDelay(pdMS_TO_TICKS(250));
    for (;;) {
        einkHandler_APP();
        vTaskDelay(pdMS_TO_TICKS(50));
        yield();
    }
}

// APP_INIT
void APP_INIT() {
    library_init();
    updateOLED();
}

// processKB_APP
void processKB_APP() {
    char ch = KB().updateKeypress();

    // Touch bar handling (runs every cycle in reading mode)
    if (g_appMode == MODE_READING) {
        touch_scroll();
    }

    if (!ch) return;

    // System-level commands (always handled)
    if (ch == 18) {  // FN key toggle
        KB().setKeyboardState(KB().getKeyboardState() == FUNC ? NORMAL : FUNC);
        return;
    }
    if (ch == 12) {  // FN+< is save & exit to OS
        KB().setKeyboardState(NORMAL);
        if (g_appMode == MODE_READING || g_appMode == MODE_TOC || g_appMode == MODE_JUMP) {
            saveBookmark();
        }
        rebootToPocketMage();
        return;
    }
    if (ch == 27 || ch == 65) {  // ESC or A is exit to OS
        if (g_appMode == MODE_READING || g_appMode == MODE_TOC || g_appMode == MODE_JUMP) {
            saveBookmark();
        }
        rebootToPocketMage();
        return;
    }
    if (ch == 17) {  // SHIFT
        KB().setKeyboardState(KB().getKeyboardState() == SHIFT ? NORMAL : SHIFT);
        return;
    }

    // Dispatch to current mode
    switch (g_appMode) {
        case MODE_LIBRARY: library_process_key(ch); break;
        case MODE_READING: reader_process_key(ch);  break;
        case MODE_TOC:     toc_process_key(ch);      break;
        case MODE_JUMP:    jump_process_key(ch);     break;
        case MODE_HELP:    g_appMode = MODE_READING; g_needsRedraw = true; break;
    }

    updateOLED();
}

// einkHandler_APP
void einkHandler_APP() {
    if (!g_needsRedraw) { updateOLED(); return; }
    g_needsRedraw = false;
    switch (g_appMode) {
        case MODE_HELP:    help_render();    break;
        case MODE_LIBRARY: library_render(); break;
        case MODE_TOC:     toc_render();     break;
        case MODE_JUMP:    jump_render();    break;
        case MODE_READING: reader_render();  break;
    }
    updateOLED();
}

#endif // OTA_APP
