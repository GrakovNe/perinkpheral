/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "back_switch.h"

back_switch_t back_switch_press(back_switch_state_t *state, scene_button_t button, scene_press_t press)
{
    if (button == SCENE_BUTTON_BACK) {
        if (press == SCENE_PRESS_DOWN) {
            state->back_is_held = true;
            state->chorded = false;
            return BACK_SWITCH_PASS;
        }
        if (press == SCENE_PRESS_UP) {
            state->back_is_held = false;
            return BACK_SWITCH_PASS;
        }
        /* The hold switched applications: it is neither BACK nor the way to the menu. The mark
         * stays until BACK goes down again, whichever of its release and its press comes first. */
        return state->chorded ? BACK_SWITCH_SWALLOW : BACK_SWITCH_PASS;
    }
    if (button != SCENE_BUTTON_UP && button != SCENE_BUTTON_DOWN) {
        return BACK_SWITCH_PASS;
    }
    uint8_t bit = (uint8_t)(1u << button);
    if (press == SCENE_PRESS_DOWN) {
        if (!state->back_is_held) {
            state->swallowed &= (uint8_t)~bit;
            return BACK_SWITCH_PASS;
        }
        /* On the way down, not on the release: the panel is slow enough already. */
        state->chorded = true;
        state->swallowed |= bit;
        return button == SCENE_BUTTON_UP ? BACK_SWITCH_PREVIOUS : BACK_SWITCH_NEXT;
    }
    /* The rest of a press that began in a chord, BACK let go before it or not. */
    return (state->swallowed & bit) != 0 ? BACK_SWITCH_SWALLOW : BACK_SWITCH_PASS;
}
