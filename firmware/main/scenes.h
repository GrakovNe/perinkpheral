/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#pragma once

#include <stdint.h>

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

/* The splash, on the whole screen, until a press. */
scene_t *scene_splash(void);

/* A scene sets a timer through this; 0 ms cancels. */
typedef void (*scenes_set_timer_fn)(int id, int32_t milliseconds);

/* The splash at power-up: it gives the screen to `next` by itself after
 * a few seconds, or at once on a press. */
scene_t *scene_boot_splash(scene_t *next, scenes_set_timer_fn set_timer);

/* How to give the device a Wi-Fi network: shown when it has none. */
scene_t *scene_setup(void);

#ifdef __cplusplus
}
#endif
