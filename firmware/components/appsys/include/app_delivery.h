/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "scene.h"
#include "wm.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Handing an answer from the network to the application it is for,
 * whether that application is on the screen or not.
 *
 * On the screen it simply gets the call. Off the screen there is no room
 * for it next to whatever is on the screen: that scene is suspended, the
 * owner is loaded for the delivery and unloaded after it, and the scene
 * comes back - unless the owner asked for the screen, and got it. The
 * frame is not touched in between. Pure, host-tested with a real module. */

/* An application that asked for the screen is not given it again sooner
 * than this: two of them must not fight over it. A long press on BACK
 * takes it from both. */
#define APP_FOCUS_MIN_GAP_MS 30000

typedef enum {
    APP_DELIVERY_PIECE,  /* a piece of an answer as it came: app_on_data() */
    APP_DELIVERY_VALUES, /* the values picked out of an answer: app_on_values() */
    APP_DELIVERY_ALARM,    /* the alarm it set has come: app_on_alarm() */
    APP_DELIVERY_SETTINGS, /* the user changed its settings: app_on_settings() */
} app_delivery_kind_t;

typedef struct {
    app_delivery_kind_t kind;
    int                 request;     /* the poll's number in the manifest */
    int                 http_status; /* VALUES: 0 when the request failed */
    /* PIECE */
    const uint8_t *data;
    size_t         length;
    int            flags; /* HOST_DATA_* */
    /* VALUES */
    const char *const *values;
    int                value_count;
} app_delivery_t;

typedef struct {
    scene_t *in_background; /* the owner loaded off the screen, between its first piece and its last */
    bool     has_given_focus;
    int64_t  last_focus_ms;
} app_delivery_state_t;

void app_delivery_init(app_delivery_state_t *state);

/* `owner` is the scene of the application the answer is for. */
void app_deliver(app_delivery_state_t *state, wm_t *wm, scene_t *owner,
                 const app_delivery_t *delivery, int64_t now_ms);

#ifdef __cplusplus
}
#endif
