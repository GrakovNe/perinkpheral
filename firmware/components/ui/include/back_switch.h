/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 *
 * BACK held and UP or DOWN pressed: the previous or the next application of the menu, without
 * the menu. The presses of such a chord are Perinkpheral OS's and reach no scene; BACK alone is
 * what it was - a short press for the scene, a long one for the menu.
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
} back_switch_t;

typedef struct {
    bool    back_is_held;
    bool    chorded;   /* this hold of BACK switched: its own short or long press is void */
    uint8_t swallowed; /* UP and DOWN that went down in a chord, until they go down again */
} back_switch_state_t;

/* What one event of one button is. Pure: the caller opens the application. */
back_switch_t back_switch_press(back_switch_state_t *state, scene_button_t button, scene_press_t press);
