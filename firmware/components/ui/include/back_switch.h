/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 *
 * BACK held and UP or DOWN pressed: the previous or the next application of the menu, without
 * the menu. The presses of such a chord are Perinkpheral OS's and reach no scene; BACK alone is
 * a short press for the scene and a long one for the menu. The long one acts when the button is
 * let go, so that a hold of any length can still become a chord.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "scene.h"

typedef enum {
    BACK_SWITCH_PASS,     /* not part of a chord: the scene's, or the long BACK */
    BACK_SWITCH_SWALLOW,  /* part of a chord: nobody's */
    BACK_SWITCH_PREVIOUS, /* open the application before this one */
    BACK_SWITCH_NEXT,     /* open the one after it */
    BACK_SWITCH_LONG_BACK, /* BACK let go after a long hold without a chord: the long BACK, now */
} back_switch_t;

typedef struct {
    bool    back_is_held;
    bool    chorded;   /* this hold of BACK switched: its own short or long press is void */
    bool    long_is_due; /* this hold passed the long press's time: the release is the long BACK */
    uint8_t swallowed; /* UP and DOWN that went down in a chord, until they go down again */
} back_switch_state_t;

/* What one event of one button is. Pure: the caller opens the application. `can_switch` is
 * false where there is nothing to switch from - the menu, whose UP and DOWN stay its own. */
back_switch_t back_switch_press(back_switch_state_t *state, scene_button_t button, scene_press_t press,
                                bool can_switch);
