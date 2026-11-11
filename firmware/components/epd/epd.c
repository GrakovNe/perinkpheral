/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "epd.h"

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/* The sequences are those of waveshareteam/e-Paper, epd4in2_V2
 * (Init, Display_Base, Display_Partial), byte for byte. See
 * hardware/README.md for what happens when they are "improved". */

static const char *TAG = "epd";

#define BUSY_INIT_MS    1000
#define BUSY_FULL_MS    4000
#define BUSY_PARTIAL_MS 3000

static epd_config_t        s_cfg;
#define BUS_HOLD_FROM_BYTES 64

static spi_device_handle_t s_spi;
static bool                s_ready;

/* CS low, one byte, CS high: holding CS over a block of data makes the
 * refresh on this module never finish. */
static void tx(const uint8_t *d, size_t n)
{
    /* A frame is fifteen thousand one-byte transactions: with the bus held
     * for all of them the driver skips its own preparation on each. What
     * goes over the wire is the same, CS and all. */
    bool holds_bus = n >= BUS_HOLD_FROM_BYTES &&
                     spi_device_acquire_bus(s_spi, portMAX_DELAY) == ESP_OK;
    for (size_t i = 0; i < n; i++) {
        spi_transaction_t t = {
            .flags = SPI_TRANS_USE_TXDATA, .length = 8, .tx_data = { d[i] },
        };
        gpio_set_level(s_cfg.cs_gpio, 0);
        ESP_ERROR_CHECK(spi_device_polling_transmit(s_spi, &t));
        gpio_set_level(s_cfg.cs_gpio, 1);
    }
    if (holds_bus) {
        spi_device_release_bus(s_spi);
    }
}

static void cmd(uint8_t c)
{
    gpio_set_level(s_cfg.dc_gpio, 0);
    tx(&c, 1);
}

static void data(uint8_t v)
{
    gpio_set_level(s_cfg.dc_gpio, 1);
    tx(&v, 1);
}

/* BUSY is high while the controller works and stays high for good in
 * deep sleep, so every wait is bounded. */
static bool wait_busy(const char *what, int timeout_ms)
{
    int ms = 0;
    vTaskDelay(pdMS_TO_TICKS(1));
    while (gpio_get_level(s_cfg.busy_gpio) == 1) {
        vTaskDelay(pdMS_TO_TICKS(10));
        ms += 10;
        if (ms > timeout_ms) {
            ESP_LOGW(TAG, "%s: BUSY still high after %d ms", what, timeout_ms);
            return false;
        }
    }
    ESP_LOGD(TAG, "%s: busy %d ms", what, ms);
    return true;
}

static void hw_reset(void)
{
    gpio_set_level(s_cfg.rst_gpio, 1);
    vTaskDelay(pdMS_TO_TICKS(100));
    gpio_set_level(s_cfg.rst_gpio, 0);
    vTaskDelay(pdMS_TO_TICKS(2));
    gpio_set_level(s_cfg.rst_gpio, 1);
    vTaskDelay(pdMS_TO_TICKS(100));
    wait_busy("hw reset", BUSY_INIT_MS);
}

static void set_window(void)
{
    cmd(0x11); data(0x03);                                /* X+, Y+ */
    cmd(0x44); data(0x00); data((EPD_W - 1) >> 3);        /* X: 0..49 */
    cmd(0x45); data(0x00); data(0x00);                    /* Y: 0..299 */
    data((EPD_H - 1) & 0xFF); data((EPD_H - 1) >> 8);
    cmd(0x4E); data(0x00);
    cmd(0x4F); data(0x00); data(0x00);
}

static void write_ram(uint8_t reg, const uint8_t *fb)
{
    cmd(reg);
    gpio_set_level(s_cfg.dc_gpio, 1);
    tx(fb, EPD_FB_SIZE);
}

static esp_err_t finish(const char *what, int timeout_ms)
{
    bool released = wait_busy(what, timeout_ms);
    if (!released) {
        hw_reset();
    }
    cmd(0x10); data(0x01); /* deep sleep mode 1, RAM kept */
    vTaskDelay(pdMS_TO_TICKS(100));
    return released ? ESP_OK : ESP_ERR_TIMEOUT;
}

esp_err_t epd_init(const epd_config_t *cfg)
{
    s_cfg = *cfg;

    gpio_config_t out = {
        .pin_bit_mask = (1ULL << cfg->dc_gpio) | (1ULL << cfg->rst_gpio) |
                        (1ULL << cfg->cs_gpio),
        .mode = GPIO_MODE_OUTPUT,
    };
    esp_err_t err = gpio_config(&out);
    if (err != ESP_OK) {
        return err;
    }
    gpio_set_level(cfg->cs_gpio, 1);
    gpio_set_level(cfg->rst_gpio, 1);

    gpio_config_t in = {
        .pin_bit_mask = 1ULL << cfg->busy_gpio,
        .mode = GPIO_MODE_INPUT,
    };
    err = gpio_config(&in);
    if (err != ESP_OK) {
        return err;
    }

    spi_bus_config_t bus = {
        .mosi_io_num = cfg->mosi_gpio,
        .miso_io_num = -1,
        .sclk_io_num = cfg->sclk_gpio,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 64,
    };
    err = spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_DISABLED);
    if (err != ESP_OK) {
        return err;
    }
    /* The one device on the bus; CS is a plain GPIO (see tx()). */
    spi_device_interface_config_t dev = {
        .clock_speed_hz = (int)cfg->spi_hz,
        .mode = 0,
        .spics_io_num = -1,
        .queue_size = 1,
    };
    err = spi_bus_add_device(SPI2_HOST, &dev, &s_spi);
    if (err != ESP_OK) {
        return err;
    }
    s_ready = true;
    ESP_LOGI(TAG, "SSD1683 %dx%d on SPI2 at %lu Hz", EPD_W, EPD_H,
             (unsigned long)cfg->spi_hz);
    return ESP_OK;
}

esp_err_t epd_show_full(const uint8_t *fb)
{
    if (!s_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    hw_reset();
    cmd(0x12);
    wait_busy("sw reset", BUSY_INIT_MS);
    cmd(0x21); data(0x40); data(0x00); /* display update control */
    cmd(0x3C); data(0x05);             /* border waveform */
    set_window();
    wait_busy("init", BUSY_INIT_MS);

    write_ram(0x24, fb);
    write_ram(0x26, fb);
    cmd(0x22); data(0xF7);
    cmd(0x20);
    return finish("full refresh", BUSY_FULL_MS);
}

esp_err_t epd_show_partial(const uint8_t *fb)
{
    if (!s_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    hw_reset();
    cmd(0x3C); data(0x80);
    cmd(0x21); data(0x00); data(0x00);
    cmd(0x3C); data(0x80);
    set_window();

    write_ram(0x24, fb);
    cmd(0x22); data(0xFF);
    cmd(0x20);
    return finish("partial refresh", BUSY_PARTIAL_MS);
}
