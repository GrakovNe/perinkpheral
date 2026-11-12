/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#pragma once

#include <stdint.h>

#include "esp_err.h"

#include "frame_box.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Waveshare 4.2" e-Paper Module V2: SSD1683, 400x300, 1 bpp. */
#define EPD_W       400
#define EPD_H       300
#define EPD_FB_SIZE (EPD_W / 8 * EPD_H)

typedef struct {
    int      mosi_gpio;
    int      sclk_gpio;
    int      cs_gpio;
    int      dc_gpio;
    int      rst_gpio;
    int      busy_gpio;
} epd_config_t;

/* Bus and pins only; the panel is not touched until the first show. */
esp_err_t epd_init(const epd_config_t *cfg);

/* Frame: row-major, MSB is the left pixel, 1 = white. A full show ends
 * with the panel in deep sleep; the next show wakes it with a hardware
 * reset. `is_fast`: Waveshare's fast full refresh, half a second shorter;
 * not for the cold.
 * ESP_ERR_TIMEOUT: BUSY did not release in time, the refresh was cut
 * with a reset and the image is whatever the panel got to. */
esp_err_t epd_show_full(const uint8_t *fb, bool is_fast);

/* Partial refresh, no flash: the part of the frame inside `window` is
 * sent and the panel is refreshed as it always is - Waveshare's
 * PartialDisplay. The window is in bytes across and rows down and has to
 * lie inside the frame. Needs a full show before it and one every few
 * updates to clear ghosting.
 * `stays_awake`: the panel is not put into deep sleep after it, and the
 * next partial show then needs no reset - 0.3 s less of each. Whoever
 * asks for this owes the panel an epd_sleep() soon: awake is not a state
 * to leave it in. */
esp_err_t epd_show_partial(const uint8_t *fb, const frame_box_t *window, bool stays_awake);

/* Puts a panel that was left awake into deep sleep; nothing if it is there. */
void epd_sleep(void);

/* The chip is about to sleep deeply and lets go of its lines: the panel's
 * reset and chip select keep their levels until epd_init() after it. */
void epd_keep_lines_through_deep_sleep(void);

#ifdef __cplusplus
}
#endif
