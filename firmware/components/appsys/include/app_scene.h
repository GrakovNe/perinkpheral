/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "app.h"
#include "scene.h"

#ifdef __cplusplus
extern "C" {
#endif

/* A wasm application as a scene: to the window manager it is a scene like
 * the menu. The module is loaded when the scene takes the screen and
 * unloaded when it leaves, so there is never more than one application in
 * memory. */

/* Where an application's code is. */
typedef struct {
    const char    *title;
    const uint8_t *wasm;
    size_t         wasm_size;
    bool           fullscreen;
} app_image_t;

/* NULL when there is no memory for it. image and services must outlive
 * the scene. */
scene_t *app_scene_create(const app_image_t *image, const app_services_t *services);

#ifdef __cplusplus
}
#endif
