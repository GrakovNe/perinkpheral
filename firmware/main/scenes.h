/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#pragma once

#include "scene.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The host's own scenes. Each one ties a model to a view and decides what
 * the buttons do; none of them draws a pixel itself or knows about the
 * panel. */

/* Something the menu can open. */
typedef struct {
    const char *title;
    const char *hint;
    scene_t    *scene;
} menu_entry_t;

/* entries must outlive the scene. */
scene_t *scene_menu(const menu_entry_t *entries, int count);

/* The device's name, firmware, memory. */
scene_t *scene_device(void);

/* The splash, on the whole screen. */
scene_t *scene_splash(void);

#ifdef __cplusplus
}
#endif
