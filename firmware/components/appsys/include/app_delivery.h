/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "json_pick.h"
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

struct host_answer_info; /* host_answer_info_t of host_api.h */

typedef enum {
    APP_DELIVERY_PIECE,  /* a piece of an answer as it came: app_on_data() */
    APP_DELIVERY_VALUES, /* the values picked out of an answer: app_on_picked() */
    APP_DELIVERY_INTENT,   /* news it asked for: app_on_intent(); `request` is which */
    APP_DELIVERY_ALARM,    /* the alarm it set has come: app_on_alarm() */
    APP_DELIVERY_SETTINGS, /* the user changed its settings: app_on_settings() */
    APP_DELIVERY_INSTALLED, /* it is new on this device: app_on_installed() */
    APP_DELIVERY_UPDATED,   /* it replaced another version of itself: app_on_updated() */
} app_delivery_kind_t;

typedef struct {
    /* After loading, before entering the handler (including a trapping handler). */
    void (*loaded)(void *context);
    void *loaded_context;
    app_delivery_kind_t kind;
    int                 request;     /* the poll's number in the manifest; the alarm's, for an alarm */
    int                 http_status; /* or, when the request failed, a HOST_FAILED_* */
    /* Not worth loading an application for: it is given only to one that
     * is on the screen. News that there is nothing new. */
    bool only_on_screen;
    /* PIECE and VALUES: what else is known of the answer, or NULL */
    const struct host_answer_info *response;
    /* PIECE and VALUES of a poll with "each": the item of the list it went
     * over. Without has_item, none. */
    bool has_item;
    int  item;
    /* PIECE */
    const uint8_t *data;
    size_t         length;
    int            flags; /* HOST_DATA_* */
    /* VALUES: what was picked out of the answer, or NULL */
    json_pick_t *picked;
} app_delivery_t;

/* For how long after its settings were saved an application on the screen
 * is shown at once whatever arrives for it. */
#define APP_SETTINGS_SHOWN_SOON_MS (30 * 1000)

/* For how long after a press of the user's an application in the background does not take the
 * screen: somebody is reading or setting something, and screen_request() would take it from
 * under their hands. What cannot wait is a notification_post(). */
#define APP_FOCUS_AFTER_PRESS_MS (30 * 1000)

typedef struct {
    scene_t *in_background; /* the owner loaded off the screen, between its first piece and its last */
    bool     is_mid_answer; /* pieces of an answer are still to come for it */
    int      answer_request; /* the number that answer is delivered under */
    bool     has_given_focus;
    int64_t  last_focus_ms;
    int64_t  last_press_ms; /* the user's last press: 0 for none yet */
    /* Whose settings were saved last, and when: only ever compared. */
    const scene_t *settings_owner;
    int64_t        settings_ms;
} app_delivery_state_t;

void app_delivery_init(app_delivery_state_t *state);

/* The user pressed something while an application takes an answer off
 * the screen, piece by piece: it is unloaded and the scene comes back to
 * take the press. What is left of the answer is dropped, and `abandoned`
 * says whose answer that was, so that whoever asked can ask again; its
 * `owner` is NULL when no answer was being taken. */
typedef struct {
    scene_t *owner;
    int      request;
} app_abandoned_t;

/* A press of the user's, at `now_ms`. */
void app_delivery_note_press(app_delivery_state_t *state, int64_t now_ms);

void app_delivery_abort(app_delivery_state_t *state, wm_t *wm, int64_t now_ms,
                           app_abandoned_t *abandoned);

/* `owner` is the scene of the application the answer is for. */
void app_deliver(app_delivery_state_t *state, wm_t *wm, scene_t *owner,
                 const app_delivery_t *delivery, int64_t now_ms);

#ifdef __cplusplus
}
#endif
