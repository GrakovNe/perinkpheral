/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "display.h"

#include <string.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "sdkconfig.h"

#include "epd.h"
#include "deadlines.h"
#include "frame_box.h"

_Static_assert(DISPLAY_WIDTH == EPD_W && DISPLAY_HEIGHT == EPD_H,
               "the display service is sized for this panel");

static const char *TAG = "display";

/* How long the panel is left out of deep sleep after a partial refresh,
 * for the next one to come without a reset: a row of pictures, steps
 * through a menu. */
#define PANEL_AWAKE_MS     1500
#define WHITE_BYTE         0xFF

static uint8_t           s_shown[DISPLAY_FRAME_SIZE];
/* False after a reset and after a failed refresh: the panel holds
 * something else than s_shown, so the next frame goes out even when it
 * looks unchanged. */
static bool              s_shown_is_on_panel;
static refresh_policy_t  s_policy;
static SemaphoreHandle_t s_lock;

static refresh_policy_cfg_t s_cfg;

#define FRAME_COLUMNS (DISPLAY_WIDTH / 8)

static const frame_box_t WHOLE_FRAME = { 0, 0, FRAME_COLUMNS, DISPLAY_HEIGHT };

/* Where the refresh before this one changed the picture. A partial
 * refresh sends only the part of the frame that changed, and that part
 * is taken together with this one: whether the controller, when it is
 * done, copies the frame it was sent over the one it compares with or
 * swaps the two, the two it holds are then the same everywhere else. */
static frame_box_t s_changed_last_time;

/* What a partial refresh has to send for the panel to show `frame`. */
static frame_box_t window_for(const uint8_t *frame)
{
    if (!s_shown_is_on_panel) {
        return WHOLE_FRAME;
    }
    frame_box_t changed = frame_box_of_change(s_shown, frame, FRAME_COLUMNS, DISPLAY_HEIGHT);
    frame_box_t window = frame_box_join(&changed, &s_changed_last_time);
    return frame_box_is_empty(&window) ? WHOLE_FRAME : window;
}

static int64_t now_ms(void)
{
    return esp_timer_get_time() / 1000;
}

/* With the lock held: the refresh that is due when nothing else was,
 * counted from this one. */
static void expect_keepalive(void)
{
    deadlines_set_in(DEADLINE_PANEL_KEEPALIVE, 0, 0, (int64_t)s_cfg.keepalive_ms);
}

/* Asks the policy and drives the panel. REFRESH_NONE: the policy said
 * not now. Call with the lock held. */
static refresh_kind_t refresh_panel(const uint8_t *frame, refresh_reason_t why, esp_err_t *err)
{
    int64_t started_ms = now_ms();
    refresh_kind_t kind = refresh_policy_decide(&s_policy, &s_cfg, why, started_ms);
    if (kind == REFRESH_NONE) {
        return kind;
    }
    frame_box_t changed = s_shown_is_on_panel
                              ? frame_box_of_change(s_shown, frame, FRAME_COLUMNS, DISPLAY_HEIGHT)
                              : WHOLE_FRAME;
    frame_box_t window = WHOLE_FRAME;
    if (kind == REFRESH_FULL) {
        *err = epd_show_full(frame, s_cfg.full_is_fast);
        deadlines_cancel(DEADLINE_PANEL_SLEEP, 0, 0);
    } else {
        window = window_for(frame);
        *err = epd_show_partial(frame, &window, true);
        if (*err == ESP_OK) {
            deadlines_set_in(DEADLINE_PANEL_SLEEP, 0, 0, PANEL_AWAKE_MS);
        } else {
            epd_sleep(); /* whatever went wrong, not left awake */
            deadlines_cancel(DEADLINE_PANEL_SLEEP, 0, 0);
        }
    }
    /* A full refresh leaves the controller's two frames the same. */
    s_changed_last_time = kind == REFRESH_FULL ? (frame_box_t){ 0, 0, 0, 0 } : changed;
    if (*err != ESP_OK) {
        s_changed_last_time = WHOLE_FRAME;
    }
    int64_t finished_ms = now_ms();
    /* Counted from the start: something that redraws on the minute would
     * otherwise come a refresh too early every time and slip later and
     * later. */
    refresh_policy_note(&s_policy, kind, why, started_ms);
    expect_keepalive();

    const char *kind_name = kind == REFRESH_FULL ? "full" : "partial";
    long long took_ms = (long long)(finished_ms - started_ms);
    if (*err == ESP_OK) {
        ESP_LOGI(TAG, "%s refresh, %d bytes, %lld ms", kind_name, window.columns * window.rows,
                 took_ms);
    } else {
        ESP_LOGW(TAG, "%s refresh: %s after %lld ms", kind_name, esp_err_to_name(*err), took_ms);
        /* What the panel holds is half a picture: the next refresh is a
         * full one, as the first after power-up is. */
        refresh_policy_init(&s_policy);
    }
    return kind;
}

void display_keepalive(void)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    esp_err_t err = ESP_OK;
    if (refresh_panel(s_shown, REFRESH_FOR_KEEPALIVE, &err) != REFRESH_NONE) {
        s_shown_is_on_panel = err == ESP_OK;
    } else {
        /* Came early - the setting was changed since: at its time, then. */
        deadlines_set_in(DEADLINE_PANEL_KEEPALIVE, 0, 0,
                         refresh_policy_wait_ms(&s_policy, &s_cfg, REFRESH_FOR_KEEPALIVE,
                                                now_ms()));
    }
    xSemaphoreGive(s_lock);
}

void display_rest(void)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    epd_sleep();
    deadlines_cancel(DEADLINE_PANEL_SLEEP, 0, 0);
    xSemaphoreGive(s_lock);
}

void display_set_policy(const refresh_policy_cfg_t *policy)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_cfg = *policy;
    /* The hours may be fewer now: looked at when the old ones would have
     * run out is too late. */
    deadlines_set_in(DEADLINE_PANEL_KEEPALIVE, 0, 0,
                     refresh_policy_wait_ms(&s_policy, &s_cfg, REFRESH_FOR_KEEPALIVE, now_ms()));
    xSemaphoreGive(s_lock);
}

void display_start(const refresh_policy_cfg_t *policy)
{
    s_cfg = *policy;
    memset(s_shown, WHITE_BYTE, sizeof s_shown);
    s_shown_is_on_panel = false;
    s_changed_last_time = WHOLE_FRAME;
    refresh_policy_init(&s_policy);
    s_lock = xSemaphoreCreateMutex();
    configASSERT(s_lock != NULL);

    epd_config_t cfg = {
        .mosi_gpio = CONFIG_APP_EPD_MOSI_GPIO,
        .sclk_gpio = CONFIG_APP_EPD_SCLK_GPIO,
        .cs_gpio = CONFIG_APP_EPD_CS_GPIO,
        .dc_gpio = CONFIG_APP_EPD_DC_GPIO,
        .rst_gpio = CONFIG_APP_EPD_RST_GPIO,
        .busy_gpio = CONFIG_APP_EPD_BUSY_GPIO,
    };
    ESP_ERROR_CHECK(epd_init(&cfg));
    expect_keepalive();
}

display_result_t display_show(const uint8_t *frame, refresh_reason_t why)
{
    display_result_t result;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (s_shown_is_on_panel && memcmp(s_shown, frame, sizeof s_shown) == 0) {
        result = DISPLAY_UNCHANGED;
    } else {
        esp_err_t err = ESP_OK;
        if (refresh_panel(frame, why, &err) == REFRESH_NONE) {
            result = DISPLAY_DEFERRED;
        } else if (err != ESP_OK) {
            s_shown_is_on_panel = false;
            result = DISPLAY_FAILED;
        } else {
            memcpy(s_shown, frame, sizeof s_shown);
            s_shown_is_on_panel = true;
            result = DISPLAY_SHOWN;
        }
    }
    xSemaphoreGive(s_lock);
    return result;
}

uint32_t display_wait_ms(refresh_reason_t why)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    uint32_t wait_ms = refresh_policy_wait_ms(&s_policy, &s_cfg, why, now_ms());
    xSemaphoreGive(s_lock);
    return wait_ms;
}

const uint8_t *display_shown_frame(void)
{
    return s_shown;
}
