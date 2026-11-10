/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#pragma once

#include <stdint.h>

#include "esp_err.h"

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
    uint32_t spi_hz;
} epd_config_t;

/* Bus and pins only; the panel is not touched until the first show. */
esp_err_t epd_init(const epd_config_t *cfg);

/* Frame: row-major, MSB is the left pixel, 1 = white. Every show ends with
 * the panel in deep sleep; the next show wakes it with a hardware reset.
 * ESP_ERR_TIMEOUT: BUSY did not release in time, the refresh was cut
 * with a reset and the image is whatever the panel got to. */
esp_err_t epd_show_full(const uint8_t *fb);

/* Whole-frame partial refresh, no flash. Needs a full show before it
 * and one every few updates to clear ghosting. */
esp_err_t epd_show_partial(const uint8_t *fb);

#ifdef __cplusplus
}
#endif
