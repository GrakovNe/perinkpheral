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
    state->has_given_focus = false;
    state->last_focus_ms = 0;
}

static void call(app_t *app, const app_delivery_t *delivery)
{
    if (delivery->kind == APP_DELIVERY_SETTINGS) {
        app_settings_changed(app);
    } else if (delivery->kind == APP_DELIVERY_VALUES) {
        app_values(app, delivery->request, delivery->http_status, delivery->values,
                   delivery->value_count);
    } else {
        app_data(app, delivery->request, delivery->data, delivery->length, delivery->flags,
                 delivery->http_status);
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
    if (wants_screen && may_give_focus(state, now_ms)) {
        state->has_given_focus = true;
        state->last_focus_ms = now_ms;
        wm_show(wm, owner);
    } else {
        wm_resume(wm);
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
            wm_scene_reacted(wm, owner, app_scene_take_reaction(owner), WM_CHANGED_BY_DATA);
        }
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
    if (is_last(delivery)) {
        end_background(state, wm, app, now_ms);
    }
}
