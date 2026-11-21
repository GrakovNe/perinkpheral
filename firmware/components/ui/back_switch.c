/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "back_switch.h"

back_switch_t back_switch_press(back_switch_state_t *state, scene_button_t button, scene_press_t press,
                                bool can_switch)
{
    if (button == SCENE_BUTTON_BACK) {
        if (press == SCENE_PRESS_DOWN) {
            state->back_is_held = true;
            state->chorded = false;
            state->long_is_due = false;
            return BACK_SWITCH_PASS;
        }
        if (press == SCENE_PRESS_UP) {
            bool was_long = state->long_is_due && !state->chorded;
            /* A short hold's SHORT comes after its release and is the chord's last event; a
             * long hold's LONG came already, and nothing of the chord follows. */
            state->chorded = state->chorded && !state->long_is_due;
            state->back_is_held = false;
            state->long_is_due = false;
            return was_long ? BACK_SWITCH_LONG_BACK : BACK_SWITCH_PASS;
        }
        /* The long BACK waits for the release: acted on while the button is held, it took the
         * menu's way before a second finger reached UP or DOWN. A long press with no button
         * down is a tool's and has no release to wait for. */
        if (press == SCENE_PRESS_LONG && state->back_is_held) {
            state->long_is_due = true;
            return BACK_SWITCH_SWALLOW;
        }
        /* The hold switched applications: it is neither BACK nor the way to the menu. Once BACK
         * is let go this is the chord's last event; a press a tool injects later is whole. */
        if (state->chorded) {
            state->chorded = state->back_is_held;
            return BACK_SWITCH_SWALLOW;
        }
        return BACK_SWITCH_PASS;
    }
    if (button != SCENE_BUTTON_UP && button != SCENE_BUTTON_DOWN) {
        return BACK_SWITCH_PASS;
    }
    uint8_t bit = (uint8_t)(1u << button);
    if (press == SCENE_PRESS_DOWN) {
        state->swallowed_long &= (uint8_t)~bit;
        if (!state->back_is_held || !can_switch) {
            state->swallowed &= (uint8_t)~bit;
            return BACK_SWITCH_PASS;
        }
        /* On the way down, not on the release: the panel is slow enough already. */
        state->chorded = true;
        state->swallowed |= bit;
        return button == SCENE_BUTTON_UP ? BACK_SWITCH_PREVIOUS : BACK_SWITCH_NEXT;
    }
    /* The rest of a press that began in a chord, BACK let go before it or not. */
    if ((state->swallowed & bit) == 0) {
        return BACK_SWITCH_PASS;
    }
    /* Its last event is the SHORT after the release, or the release when the LONG came: a press
     * a tool injects after that is whole. */
    if (press == SCENE_PRESS_LONG) {
        state->swallowed_long |= bit;
    } else if (press == SCENE_PRESS_SHORT ||
               (press == SCENE_PRESS_UP && (state->swallowed_long & bit) != 0)) {
        state->swallowed &= (uint8_t)~bit;
        state->swallowed_long &= (uint8_t)~bit;
    }
    return BACK_SWITCH_SWALLOW;
}
