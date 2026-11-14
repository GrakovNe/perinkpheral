/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "epd.h"

#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_pm.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "hal/gpio_ll.h"
#include "soc/gpio_struct.h"

/* The sequences are those of waveshareteam/e-Paper, epd4in2_V2
 * (Init, Init_Fast, Display_Base, Display_Fast, Display_Partial,
 * PartialDisplay), byte for byte. See
 * hardware/README.md for what happens when they are "improved". */

static const char *TAG = "epd";

/* The commands of the SSD1683, by the names its datasheet gives them. */
enum {
    CMD_DEEP_SLEEP = 0x10,
    CMD_DATA_ENTRY_MODE = 0x11,
    CMD_SOFT_RESET = 0x12,
    CMD_WRITE_TEMPERATURE = 0x1A,
    CMD_ACTIVATE = 0x20,         /* master activation: runs the update sequence */
    CMD_UPDATE_CONTROL_1 = 0x21,
    CMD_UPDATE_CONTROL_2 = 0x22, /* which sequence the activation runs */
    CMD_WRITE_RAM = 0x24,
    CMD_WRITE_RAM_PREVIOUS = 0x26, /* the frame a partial refresh starts from */
    CMD_BORDER_WAVEFORM = 0x3C,
    CMD_RAM_X_RANGE = 0x44,
    CMD_RAM_Y_RANGE = 0x45,
    CMD_RAM_X_COUNTER = 0x4E,
    CMD_RAM_Y_COUNTER = 0x4F,
};

/* What goes with them: Waveshare's values for the 4.2" V2, as its code has them. */
#define ENTRY_X_AND_Y_UP        0x03
#define SLEEP_KEEPING_RAM       0x01 /* deep sleep mode 1 */
#define UPDATE_1_FULL           0x40 /* with a second byte of 0x00 */
#define UPDATE_1_PARTIAL        0x00
#define UPDATE_1_SECOND         0x00
#define BORDER_FOR_FULL         0x05
#define BORDER_FOR_PARTIAL      0x80
#define TEMPERATURE_FOR_FAST    0x6E /* the waveform of a warmer panel: the shorter one */
#define SEQUENCE_LOAD_WAVEFORM  0x91
#define SEQUENCE_FULL           0xF7
#define SEQUENCE_FULL_FAST      0xC7
#define SEQUENCE_PARTIAL        0xFF

#define BUSY_INIT_MS    1000
#define BUSY_FULL_MS    4000
#define BUSY_PARTIAL_MS 3000

static epd_config_t s_cfg;
static bool         s_ready;
/* Out of deep sleep and set up for partial refreshes: the next one needs
 * no reset. The controller turns the panel's voltages off by itself at
 * the end of every refresh, awake or not. */
static bool         s_is_awake;

/* The bytes are clocked out by hand. What the panel sees is what a SPI
 * master in mode 0 would send it - the data bit set while the clock is
 * low and taken on its rise, CS low for one byte and high after it - at
 * some 3 MHz of the 20 the controller allows. The SPI driver sent the
 * same at 2 MHz and spent four times the byte itself on getting ready for
 * it: 0.3 s of every frame.
 *
 * A wait is twelve empty instructions, written out so that it is twelve
 * and not a loop around them: 75 ns at the chip's 160 MHz, longer when it
 * runs slower. The controller asks for 25 ns of each half of the clock. */
#define EMPTY_INSTRUCTION() __asm__ __volatile__("nop")

static inline __attribute__((always_inline)) void short_delay(void)
{
    EMPTY_INSTRUCTION(); EMPTY_INSTRUCTION(); EMPTY_INSTRUCTION(); EMPTY_INSTRUCTION();
    EMPTY_INSTRUCTION(); EMPTY_INSTRUCTION(); EMPTY_INSTRUCTION(); EMPTY_INSTRUCTION();
    EMPTY_INSTRUCTION(); EMPTY_INSTRUCTION(); EMPTY_INSTRUCTION(); EMPTY_INSTRUCTION();
}

static inline __attribute__((always_inline)) void set_pin(int gpio, int level)
{
    gpio_ll_set_level(&GPIO, (uint32_t)gpio, (uint32_t)level);
}

/* CS low, one byte, CS high: holding CS over a block of data makes the
 * refresh on this module never finish. */
static void tx(const uint8_t *d, size_t n)
{
    const int cs = s_cfg.cs_gpio, clock = s_cfg.sclk_gpio, mosi = s_cfg.mosi_gpio;
    for (size_t i = 0; i < n; i++) {
        uint8_t byte = d[i];
        set_pin(cs, 0);
        for (int bit = 7; bit >= 0; bit--) {
            set_pin(mosi, byte >> bit & 1);
            short_delay();
            set_pin(clock, 1);
            short_delay();
            set_pin(clock, 0);
        }
        short_delay();
        set_pin(cs, 1);
        /* CS stays high for four of them between two bytes. */
        short_delay(); short_delay(); short_delay(); short_delay();
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
    /* Two ticks are a millisecond at least; one may be no time at all,
     * and BUSY read before it rose looks like a refresh that is done. */
    vTaskDelay(2);
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

/* Waveshare's reset: high, low for 2 ms, high. The low part is 2 ms and
 * not more: the module's own circuit takes a longer one for "power off",
 * and the panel then does not answer at all. A delay of the scheduler's is
 * at least what was asked - while the flash is being erased it was twenty
 * milliseconds - so this one is counted with nothing else running. */
#define RESET_LOW_US 2000

static void hw_reset(void)
{
    static portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
    gpio_set_level(s_cfg.rst_gpio, 1);
    vTaskDelay(pdMS_TO_TICKS(100));
    portENTER_CRITICAL(&lock);
    set_pin(s_cfg.rst_gpio, 0);
    esp_rom_delay_us(RESET_LOW_US);
    set_pin(s_cfg.rst_gpio, 1);
    portEXIT_CRITICAL(&lock);
    vTaskDelay(pdMS_TO_TICKS(100));
    wait_busy("hw reset", BUSY_INIT_MS);
}

static const frame_box_t WHOLE_FRAME = EPD_WHOLE_FRAME;

/* Where the bytes that follow go: the controller counts X in bytes. */
static void set_window(const frame_box_t *window)
{
    int last_column = window->column + window->columns - 1;
    int last_row = window->row + window->rows - 1;
    cmd(CMD_DATA_ENTRY_MODE); data(ENTRY_X_AND_Y_UP);
    cmd(CMD_RAM_X_RANGE); data(window->column); data(last_column);
    cmd(CMD_RAM_Y_RANGE); data(window->row & 0xFF); data(window->row >> 8);
    data(last_row & 0xFF); data(last_row >> 8);
    cmd(CMD_RAM_X_COUNTER); data(window->column);
    cmd(CMD_RAM_Y_COUNTER); data(window->row & 0xFF); data(window->row >> 8);
}

/* The rows of the window, each from its first byte to its last. */
static void write_ram(uint8_t reg, const uint8_t *fb, const frame_box_t *window)
{
    cmd(reg);
    gpio_set_level(s_cfg.dc_gpio, 1);
    for (int row = window->row; row < window->row + window->rows; row++) {
        tx(fb + row * EPD_COLUMNS + window->column, (size_t)window->columns);
    }
}

static bool is_inside_frame(const frame_box_t *window)
{
    return window->column >= 0 && window->row >= 0 && window->columns > 0 && window->rows > 0 &&
           window->column + window->columns <= EPD_COLUMNS && window->row + window->rows <= EPD_H;
}

/* The chip does not sleep while the controller is out of its own sleep:
 * from the first byte of a show until the controller was sent to sleep.
 * The waits in between are long enough for the chip to doze off in, and
 * nothing should move on the panel's lines then. */
static esp_pm_lock_handle_t s_stay_up;
static bool                 s_stays_up;

static void stay_up(void)
{
    if (!s_stays_up && s_stay_up != NULL) {
        esp_pm_lock_acquire(s_stay_up);
        s_stays_up = true;
    }
}

static void deep_sleep(void)
{
    cmd(CMD_DEEP_SLEEP); data(SLEEP_KEEPING_RAM);
    vTaskDelay(pdMS_TO_TICKS(100));
    s_is_awake = false;
    if (s_stays_up) {
        esp_pm_lock_release(s_stay_up);
        s_stays_up = false;
    }
}

static esp_err_t finish(const char *what, int timeout_ms, bool stays_awake)
{
    bool released = wait_busy(what, timeout_ms);
    if (!released) {
        hw_reset();
        deep_sleep();
        return ESP_ERR_TIMEOUT;
    }
    if (stays_awake) {
        s_is_awake = true;
    } else {
        deep_sleep();
    }
    return ESP_OK;
}

void epd_sleep(void)
{
    if (s_ready && s_is_awake) {
        deep_sleep();
    }
}

esp_err_t epd_init(const epd_config_t *cfg)
{
    s_cfg = *cfg;

    /* The levels first: a line that becomes an output shows what is in
     * its register at once, and RST low for a moment would wake a panel
     * that sleeps. */
    gpio_set_level(cfg->cs_gpio, 1);
    gpio_set_level(cfg->rst_gpio, 1);
    gpio_set_level(cfg->sclk_gpio, 0);
    const int outputs[] = { cfg->dc_gpio, cfg->rst_gpio, cfg->cs_gpio, cfg->sclk_gpio,
                            cfg->mosi_gpio };
    gpio_config_t out = { .mode = GPIO_MODE_OUTPUT };
    for (size_t i = 0; i < sizeof outputs / sizeof outputs[0]; i++) {
        out.pin_bit_mask |= 1ULL << outputs[i];
    }
    esp_err_t err = gpio_config(&out);
    if (err != ESP_OK) {
        return err;
    }
    /* In light sleep the chip lets go of every line that is not told
     * otherwise: these keep their levels, RST above all. */
    for (size_t i = 0; i < sizeof outputs / sizeof outputs[0]; i++) {
        gpio_sleep_sel_dis(outputs[i]);
    }
    gpio_sleep_sel_dis(cfg->busy_gpio);
    esp_pm_lock_create(ESP_PM_NO_LIGHT_SLEEP, 0, "panel", &s_stay_up);

    gpio_config_t in = {
        .pin_bit_mask = 1ULL << cfg->busy_gpio,
        .mode = GPIO_MODE_INPUT,
    };
    err = gpio_config(&in);
    if (err != ESP_OK) {
        return err;
    }
    s_ready = true;
    ESP_LOGI(TAG, "SSD1683 %dx%d", EPD_W, EPD_H);
    return ESP_OK;
}

esp_err_t epd_show_full(const uint8_t *fb, bool is_fast)
{
    if (!s_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    stay_up();
    hw_reset();
    cmd(CMD_SOFT_RESET);
    wait_busy("sw reset", BUSY_INIT_MS);
    cmd(CMD_UPDATE_CONTROL_1); data(UPDATE_1_FULL); data(UPDATE_1_SECOND);
    cmd(CMD_BORDER_WAVEFORM); data(BORDER_FOR_FULL);
    if (is_fast) {
        /* Init_Fast, its 1.5 s: the waveform of a warmer panel, which is
         * a shorter one. */
        cmd(CMD_WRITE_TEMPERATURE); data(TEMPERATURE_FOR_FAST);
        cmd(CMD_UPDATE_CONTROL_2); data(SEQUENCE_LOAD_WAVEFORM);
        cmd(CMD_ACTIVATE);
        wait_busy("fast waveform", BUSY_INIT_MS);
    }
    set_window(&WHOLE_FRAME);
    wait_busy("init", BUSY_INIT_MS);

    write_ram(CMD_WRITE_RAM, fb, &WHOLE_FRAME);
    write_ram(CMD_WRITE_RAM_PREVIOUS, fb, &WHOLE_FRAME);
    cmd(CMD_UPDATE_CONTROL_2); data(is_fast ? SEQUENCE_FULL_FAST : SEQUENCE_FULL);
    cmd(CMD_ACTIVATE);
    return finish("full refresh", BUSY_FULL_MS, false);
}

esp_err_t epd_show_partial(const uint8_t *fb, const frame_box_t *window, bool stays_awake)
{
    if (!s_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!is_inside_frame(window)) {
        return ESP_ERR_INVALID_ARG;
    }
    stay_up();
    if (!s_is_awake) {
        hw_reset();
    }
    cmd(CMD_BORDER_WAVEFORM); data(BORDER_FOR_PARTIAL);
    cmd(CMD_UPDATE_CONTROL_1); data(UPDATE_1_PARTIAL); data(UPDATE_1_SECOND);
    cmd(CMD_BORDER_WAVEFORM); data(BORDER_FOR_PARTIAL);
    set_window(window);

    write_ram(CMD_WRITE_RAM, fb, window);
    cmd(CMD_UPDATE_CONTROL_2); data(SEQUENCE_PARTIAL);
    cmd(CMD_ACTIVATE);
    return finish("partial refresh", BUSY_PARTIAL_MS, stays_awake);
}
