/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "deep_sleep.h"

#include <sys/time.h>

#include "esp_log.h"
#include "esp_sleep.h"
#include "sdkconfig.h"

static const char *TAG = "sleep";

#define MICROSECONDS_PER_MS 1000
#define MS_PER_SECOND       1000

/* The lines that can wake the chip from deep sleep are GPIO0-7: the first
 * three buttons are on them, the fourth is not. */
#define WAKING_BUTTONS                                                                  \
    (1ULL << CONFIG_APP_BTN1_GPIO | 1ULL << CONFIG_APP_BTN2_GPIO | 1ULL << CONFIG_APP_BTN3_GPIO)

static wake_reason_t s_woke_by;

void deep_sleep_note_wake(void)
{
    switch (esp_sleep_get_wakeup_cause()) {
    case ESP_SLEEP_WAKEUP_TIMER:
        s_woke_by = WOKE_BY_CLOCK;
        break;
    case ESP_SLEEP_WAKEUP_EXT1:
    case ESP_SLEEP_WAKEUP_GPIO: /* the same press, told another way */
        s_woke_by = WOKE_BY_BUTTON;
        break;
    default:
        s_woke_by = WOKE_NOT_FROM_SLEEP;
        break;
    }
}

wake_reason_t deep_sleep_woke_by(void)
{
    return s_woke_by;
}

void deep_sleep_until(int64_t wake_at_ms)
{
    if (wake_at_ms != INT64_MAX) {
        struct timeval now;
        gettimeofday(&now, NULL);
        int64_t now_ms = (int64_t)now.tv_sec * MS_PER_SECOND + now.tv_usec / MICROSECONDS_PER_MS;
        int64_t sleep_ms = wake_at_ms > now_ms ? wake_at_ms - now_ms : 0;
        esp_sleep_enable_timer_wakeup((uint64_t)sleep_ms * MICROSECONDS_PER_MS);
        ESP_LOGW(TAG, "asleep for %lld s", (long long)(sleep_ms / MS_PER_SECOND));
    } else {
        ESP_LOGW(TAG, "asleep until a button");
    }
    /* The buttons wake the chip from its light sleep too, by another way
     * that would keep more of it powered through this one. */
    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_GPIO);
    /* A pressed button pulls its line low; the module has the pull-ups. */
    esp_sleep_enable_ext1_wakeup_io(WAKING_BUTTONS, ESP_EXT1_WAKEUP_ANY_LOW);
    esp_deep_sleep_start();
}
