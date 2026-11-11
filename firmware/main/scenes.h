/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#pragma once

#include <stdbool.h>
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
    const char     *title;
    const uint16_t *icon; /* APP_ICON_CELLS rows, or NULL: see app_manifest.h */
    scene_t        *scene;
} menu_entry_t;

/* entries must outlive the scene. */
scene_t *scene_menu(const menu_entry_t *entries, int count);

/* The device's name, firmware, memory. */
scene_t *scene_device(void);

/* A scene sets a timer through this; 0 ms cancels. */
typedef void (*scenes_set_timer_fn)(int id, int32_t milliseconds);

/* Whether a scene held by another has a picture of its own to draw now. */
typedef bool (*scenes_is_shown_fn)(scene_t *scene);

/* The splash at power-up: it gives the screen to `next` by itself after
 * a few seconds, or at once on a press. What it shows is `picture`, a
 * full-screen scene - the About application; when `picture_is_shown` says
 * that one cannot draw, it shows nothing and moves on at once. */
scene_t *scene_boot_splash(scene_t *next, scenes_set_timer_fn set_timer, scene_t *picture,
                           scenes_is_shown_fn picture_is_shown);

/* How to give the device a Wi-Fi network: shown when it has none. */
scene_t *scene_setup(void);

#ifdef __cplusplus
}
#endif
