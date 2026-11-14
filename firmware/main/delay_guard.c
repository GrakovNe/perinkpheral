/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "delay_guard.h"

#include "esp_attr.h"

/* ESP-IDF 5.5.5, esp_hw_support/sar_periph_ctrl_common.c, temp_sensor_get_raw_value():
 *
 *     int64_t diff = esp_timer_get_time() - timer1;
 *     if (diff < TSENS_LINE_REGRESSION_US) {
 *         esp_rom_delay_us(TSENS_LINE_REGRESSION_US - diff);
 *
 * inside a critical section. When the timer reads earlier than it did at
 * `timer1`, `diff` is negative and the delay is its low 32 bits: up to 71
 * minutes with interrupts off, ended by the interrupt watchdog. The radio
 * calls it whenever it turns its front end off (phy_close_rf, from the
 * Wi-Fi task's modem sleep). Seen on the device three times in two days;
 * the record of 2026-11-14 (crash_note.c) names the place.
 *
 * Linked with --wrap=esp_rom_delay_us: a busy wait of more than a second
 * is nobody's intention, and is made a millisecond. */

#define DELAY_ABSURD_US  (1000 * 1000)
#define DELAY_INSTEAD_US 1000

static uint32_t s_shortened;

void __real_esp_rom_delay_us(uint32_t us);

void IRAM_ATTR __wrap_esp_rom_delay_us(uint32_t us)
{
    if (us > DELAY_ABSURD_US) {
        s_shortened++;
        us = DELAY_INSTEAD_US;
    }
    __real_esp_rom_delay_us(us);
}

uint32_t delay_guard_shortened(void)
{
    return s_shortened;
}
