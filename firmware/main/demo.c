/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "demo.h"

#include <stdint.h>

#include "esp_log.h"

#include "buttons.h"
#include "display.h"
#include "screens.h"

static const char *TAG = "demo";

static uint32_t s_count[BUTTONS_COUNT];

static void show_splash(gfx_t *g)
{
    screens_splash(g);
    display_present(REFRESH_FOR_SWITCH);
}

void demo_run(void)
{
    gfx_t *g = display_gfx();

    show_splash(g);
    ESP_LOGI(TAG, "splash shown; short press -> counters, long press 4 -> splash");

    bool splash = true;
    bool dirty = false;
    for (;;) {
        buttons_event_t ev;
        if (buttons_wait(&ev, dirty ? 500 : UINT32_MAX)) {
            ESP_LOGI(TAG, "button %d %s", ev.index + 1, ev.event == BTN_LONG ? "long" : "short");
            if (ev.event == BTN_LONG && ev.index == 3) {
                /* Stands in for the host menu until there is one. */
                splash = true;
                dirty = false;
                show_splash(g);
                continue;
            }
            if (ev.event != BTN_SHORT) {
                continue;
            }
            if (splash) {
                splash = false;
                screens_counters(g, s_count);
                display_present(REFRESH_FOR_SWITCH);
                continue;
            }
            s_count[ev.index]++;
            dirty = true;
        }
        /* Presses faster than the panel may be redrawn are counted and
         * shown by the next refresh the policy allows. */
        if (dirty) {
            screens_counters(g, s_count);
            if (display_present(REFRESH_FOR_BUTTON)) {
                dirty = false;
            }
        }
    }
}
