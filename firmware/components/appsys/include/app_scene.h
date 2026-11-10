/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#pragma once

#include "app.h"
#include "app_manifest.h"
#include "scene.h"

#ifdef __cplusplus
extern "C" {
#endif

/* An installed application as a scene: to the window manager it is a scene
 * like the menu. The module is read from the store and loaded when the
 * scene takes the screen, and unloaded when it leaves, so there is never
 * more than one application in memory. */

/* NULL when there is no memory for it. store_root and services must
 * outlive the scene; the manifest is copied. */
scene_t *app_scene_create(const app_manifest_t *manifest, const char *store_root,
                          const app_services_t *services);

#ifdef __cplusplus
}
#endif
