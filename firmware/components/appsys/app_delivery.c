/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "app_delivery.h"

#include "app_scene.h"
#include "host_api.h"

void app_delivery_init(app_delivery_state_t *state)
{
    state->in_background = NULL;
    state->is_mid_answer = false;
    state->has_given_focus = false;
    state->last_focus_ms = 0;
    state->settings_owner = NULL;
    state->settings_ms = 0;
}

static void call_handler(app_t *app, const app_delivery_t *delivery)
{
    if (delivery->kind == APP_DELIVERY_ALARM) {
        app_alarm(app, delivery->request);
    } else if (delivery->kind == APP_DELIVERY_INTENT) {
        app_intent(app, delivery->request);
    } else if (delivery->kind == APP_DELIVERY_SETTINGS) {
        app_settings_changed(app);
    } else if (delivery->kind == APP_DELIVERY_VALUES) {
        app_values(app, delivery->request, delivery->http_status, delivery->values,
                   delivery->value_count);
    } else {
        app_data(app, delivery->request, delivery->data, delivery->length, delivery->flags,
                 delivery->http_status);
    }
}

static void call(app_t *app, const app_delivery_t *delivery)
{
    bool is_answer = delivery->kind == APP_DELIVERY_PIECE ||
                        delivery->kind == APP_DELIVERY_VALUES;
    if (is_answer) {
        app_answer_begins(app, delivery->response, delivery->http_status);
    }
    call_handler(app, delivery);
    if (is_answer) {
        app_answer_ends(app);
    }
}

/* Nothing more of this answer will come. */
static bool is_last(const app_delivery_t *delivery)
{
    return delivery->kind != APP_DELIVERY_PIECE ||
           (delivery->flags & (HOST_DATA_LAST | HOST_DATA_FAILED)) != 0;
}

static bool may_give_focus(const app_delivery_state_t *state, int64_t now_ms)
{
    return !state->has_given_focus || now_ms - state->last_focus_ms >= APP_FOCUS_MIN_GAP_MS;
}

static void end_background(app_delivery_state_t *state, wm_t *wm, app_t *app, int64_t now_ms)
{
    scene_t *owner = state->in_background;
    bool wants_screen = app != NULL && app_take_focus_request(app) && !app_has_stopped(app);
    app_scene_end_background(owner);
    state->in_background = NULL;
    state->is_mid_answer = false;
    if (wants_screen && may_give_focus(state, now_ms)) {
        state->has_given_focus = true;
        state->last_focus_ms = now_ms;
        wm_open(wm, owner);
    } else {
        wm_resume(wm);
    }
}

void app_delivery_abort(app_delivery_state_t *state, wm_t *wm, int64_t now_ms,
                           app_abandoned_t *abandoned)
{
    abandoned->owner = NULL;
    if (state->in_background != NULL) {
        if (state->is_mid_answer) {
            abandoned->owner = state->in_background;
            abandoned->request = state->answer_request;
        }
        end_background(state, wm, NULL, now_ms);
    }
}

void app_deliver(app_delivery_state_t *state, wm_t *wm, scene_t *owner,
                 const app_delivery_t *delivery, int64_t now_ms)
{
    bool is_on_screen = wm->current == owner && !wm->suspended;
    if (is_on_screen) {
        app_t *app = app_scene_app(owner);
        if (app != NULL) {
            call(app, delivery);
            /* On the screen there is nothing to ask for. */
            app_take_focus_request(app);
            /* Somebody has just saved its settings and is looking at the
             * device: what the settings changed is shown at once, and so is
             * the answer fetched because of them - not when the panel is
             * next due for data. */
            if (delivery->kind == APP_DELIVERY_SETTINGS) {
                state->settings_owner = owner;
                state->settings_ms = now_ms;
            }
            bool is_after_settings = state->settings_owner == owner &&
                                     now_ms - state->settings_ms < APP_SETTINGS_SHOWN_SOON_MS;
            wm_scene_reacted(wm, owner, app_scene_take_reaction(owner),
                             is_after_settings ? WM_CHANGED_URGENTLY : WM_CHANGED_BY_DATA);
        }
        /* It came onto the screen while it was loaded for an answer - the
         * user opened it: it is the scene's now, and not to be unloaded as
         * a background one when the next answer comes for somebody else. */
        if (state->in_background == owner) {
            state->in_background = NULL;
            state->is_mid_answer = false;
        }
        return;
    }

    if (delivery->only_on_screen) {
        return;
    }
    /* A piece that is not the first, for an application that is not taking
     * the answer: the instance that took the start gave way to a press, or
     * was replaced. The rest of that answer is nobody's. */
    bool is_first_piece = delivery->kind != APP_DELIVERY_PIECE ||
                      (delivery->flags & (HOST_DATA_FIRST | HOST_DATA_FAILED)) != 0;
    if (!is_first_piece && state->in_background != owner) {
        return;
    }
    app_t *app;
    if (state->in_background == owner) {
        app = app_scene_app(owner);
    } else {
        /* An answer for another application while one is still taking its
         * own does not happen - requests go one at a time - but if it did,
         * the first one is not left loaded. */
        if (state->in_background != NULL) {
            end_background(state, wm, NULL, now_ms);
        }
        wm_suspend(wm);
        app = app_scene_begin_background(owner);
        state->in_background = owner;
    }
    if (app != NULL) {
        call(app, delivery);
    }
    if (delivery->kind == APP_DELIVERY_PIECE) {
        state->is_mid_answer = !is_last(delivery);
        state->answer_request = delivery->request;
    }
    /* Something else for it in the middle of an answer - its settings
     * changed, its alarm came - is taken by the instance that is taking
     * the answer; unloading it now would lose what is still to come. */
    if (!state->is_mid_answer) {
        end_background(state, wm, app, now_ms);
    }
}
