/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "power.h"

#include "esp_log.h"
#include "driver/usb_serial_jtag.h"
#include "hal/usb_serial_jtag_ll.h"
#include "esp_pm.h"
#include "esp_private/periph_ctrl.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "deadlines.h"
#include "wifi.h"

static const char *TAG = "power";

#define CPU_MHZ             160

static SemaphoreHandle_t  s_lock;
static bool               s_is_saving;
static power_mode_t        s_mode; /* what the user chose; s_is_saving follows from it */
static int                s_radio_holds;
static bool               s_is_awake_for_user; /* the while after a press */

/* Told whenever "somebody may want to find the device" changes: always
 * while it is plugged, and for the while after a press while it saves.
 * What only serves a person - the name on the network - runs then and
 * not through every poll. */
static void (*s_reachable_changed)(bool is_reachable);
static bool s_was_reachable;
static bool s_reachable_is_told;

void power_on_reachable(void (*changed)(bool is_reachable))
{
    s_reachable_changed = changed;
}

/* Not with the lock held: whoever is told may take its time. */
static void tell_reachable(void)
{
    bool is_reachable = !s_is_saving || s_is_awake_for_user;
    if (s_reachable_changed != NULL && (!s_reachable_is_told || is_reachable != s_was_reachable)) {
        s_reachable_is_told = true;
        s_was_reachable = is_reachable;
        s_reachable_changed(is_reachable);
    }
}

/* With the lock held: the radio is on unless the device is saving and
 * nobody needs it. */
static void apply_radio(void)
{
    wifi_set_radio(!s_is_saving || s_radio_holds > 0 || s_is_awake_for_user);
}

/* Lets the chip sleep whenever nothing runs, or not at all. */
static void apply_sleep(void)
{
    esp_pm_config_t config = {
        .max_freq_mhz = CPU_MHZ,
        .min_freq_mhz = CPU_MHZ,
        .light_sleep_enable = s_is_saving,
    };
    esp_err_t result = esp_pm_configure(&config);
    if (result != ESP_OK) {
        ESP_LOGW(TAG, "sleep not %s: %s", s_is_saving ? "allowed" : "forbidden",
                 esp_err_to_name(result));
    }
}

/* The chip does not sleep at all for the while after a press when the
 * cable decides the profile: a sleeping chip's USB is off, and a computer
 * plugged in meanwhile would never be seen. Awake, it is - so "plugged it
 * into the computer and pressed a button" is always noticed. */
static esp_pm_lock_handle_t s_stay_up;
static bool                 s_is_staying_up;

/* With the lock held. */
static void stay_up(bool wanted)
{
    if (s_stay_up == NULL) {
        esp_pm_lock_create(ESP_PM_NO_LIGHT_SLEEP, 0, "cable", &s_stay_up);
    }
    if (s_stay_up == NULL || wanted == s_is_staying_up) {
        return;
    }
    s_is_staying_up = wanted;
    if (wanted) {
        esp_pm_lock_acquire(s_stay_up);
    } else {
        esp_pm_lock_release(s_stay_up);
    }
}

/* Shows itself to the computer anew. One that found the device asleep
 * when the cable went in asked it who it was, got no answer and stopped
 * asking; it asks again only of a device that has just appeared - so the
 * device goes off the bus for a moment and comes back. Not while it is
 * known to the computer already: that would drop the console. */
#define OFF_THE_BUS_MS 200

/* The USB of the chip as it is after a power-up: clocked, on its pads,
 * its own pull-up on D+ and nobody overriding it. Going to sleep the chip
 * takes its USB off the bus and puts it back on waking, and a restart
 * does not touch that part of the chip at all - what was left wrong in it
 * stays wrong through any number of restarts. So it is written out. */
static void put_usb_back(void)
{
    const usb_serial_jtag_pull_override_vals_t on_the_bus = { .dp_pu = true };
    PERIPH_RCC_ATOMIC()
    {
        usb_serial_jtag_ll_enable_bus_clock(true);
    }
    usb_serial_jtag_ll_phy_enable_pad(true);
    usb_serial_jtag_ll_phy_enable_pull_override(&on_the_bus);
    usb_serial_jtag_ll_phy_disable_pull_override();
}

/* Shows itself to the computer anew. One that found the device asleep
 * when the cable went in asked it who it was, got no answer and stopped
 * asking; it asks again only of a device that has just appeared - so the
 * device goes off the bus for a moment and comes back. Not while it is
 * known to the computer already: that would drop the console. */
#define OFF_THE_BUS_MS 200

static void show_to_the_computer(void)
{
    if (usb_serial_jtag_is_connected()) {
        return;
    }
    const usb_serial_jtag_pull_override_vals_t off_the_bus = { 0 };
    usb_serial_jtag_ll_phy_enable_pull_override(&off_the_bus);
    vTaskDelay(pdMS_TO_TICKS(OFF_THE_BUS_MS));
    put_usb_back();
}

void power_awake_is_over(void)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_is_awake_for_user = false;
    stay_up(false);
    apply_radio();
    xSemaphoreGive(s_lock);
    tell_reachable();
}

/* With the lock held. */
static void stay_awake_for_user(void)
{
    s_is_awake_for_user = true;
    stay_up(s_mode == POWER_AUTO);
    deadlines_set_in(DEADLINE_AWAKE_WINDOW, 0, 0, POWER_AWAKE_AFTER_PRESS_MS);
}

/* How often the cable is looked at while that decides anything. */
#define LOOK_EVERY_MS 5000


static bool is_to_save(power_mode_t mode)
{
    return mode == POWER_ALWAYS || (mode == POWER_AUTO && !usb_serial_jtag_is_connected());
}

static void look_again_later(void)
{
    if (s_mode == POWER_AUTO) {
        deadlines_set_in(DEADLINE_POWER_LOOK, 0, 0, LOOK_EVERY_MS);
    } else {
        deadlines_cancel(DEADLINE_POWER_LOOK, 0, 0);
    }
}

/* The device saves or does not, as of now. True when that changed. */
static bool set_saving(bool is_saving)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    bool changed = s_is_saving != is_saving;
    s_is_saving = is_saving;
    if (changed) {
        /* Whoever changed it is here: the setting was saved, or the cable
         * was plugged in or pulled out. */
        stay_awake_for_user();
        apply_radio();
    }
    xSemaphoreGive(s_lock);
    if (changed) {
        apply_sleep();
        tell_reachable();
        ESP_LOGI(TAG, "%s", is_saving ? "saving power" : "not saving power");
    }
    return changed;
}

void power_start(power_mode_t mode)
{
    s_lock = xSemaphoreCreateMutex();
    configASSERT(s_lock != NULL);
    s_mode = mode;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_is_saving = is_to_save(mode);
    /* Powered up: whoever did it may want the page. */
    stay_awake_for_user();
    apply_radio();
    xSemaphoreGive(s_lock);
    apply_sleep();
    tell_reachable();
    look_again_later();
    /* Not a knock: whether a computer is there is not known this early,
     * and one that is would lose the console at every start. */
    put_usb_back();
    ESP_LOGI(TAG, "%s", s_is_saving ? "saving power" : "not saving power");
}

void power_set_mode(power_mode_t mode)
{
    if (s_lock == NULL) {
        return;
    }
    s_mode = mode;
    set_saving(is_to_save(mode));
    look_again_later();
    show_to_the_computer();
}

bool power_look(void)
{
    if (s_lock == NULL || s_mode != POWER_AUTO) {
        return false;
    }
    bool changed = set_saving(is_to_save(POWER_AUTO));
    look_again_later();
    return changed;
}

bool power_is_saving(void)
{
    return s_is_saving;
}

void power_hold_radio(void)
{
    if (s_lock == NULL) {
        return;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_radio_holds++;
    apply_radio();
    xSemaphoreGive(s_lock);
}

void power_release_radio(void)
{
    if (s_lock == NULL) {
        return;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (s_radio_holds > 0) {
        s_radio_holds--;
    }
    apply_radio();
    xSemaphoreGive(s_lock);
}

void power_note_press(void)
{
    if (s_lock == NULL || !s_is_saving) {
        return;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    bool was_awake = s_is_awake_for_user;
    stay_awake_for_user();
    apply_radio();
    xSemaphoreGive(s_lock);
    tell_reachable();
    if (!was_awake) {
        /* The first press after a rest, and the chip is up for a while now:
         * long enough to be found. Not at every press - it takes a moment. */
        show_to_the_computer();
    }
}
