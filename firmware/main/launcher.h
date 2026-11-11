/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#pragma once

#include "app_manifest.h"
#include "scenes.h"

#ifdef __cplusplus
extern "C" {
#endif

/* What the device can show: the installed applications, each as a scene,
 * and the host's own pages. */

#define LAUNCHER_ENTRIES_MAX 26

/* Reads the store and builds the scenes; once, at start. */
void launcher_start(void);

/* The About application, which is also what the splash shows; NULL when
 * the firmware was built without a sound one. */
scene_t *launcher_about(void);

/* What the menu lists: applications by title, then the host's pages. */
const menu_entry_t *launcher_entries(int *count);

/* The installed applications that can be started, as their manifests say. */
const app_manifest_t *launcher_installed(int *count);

/* The scene of an installed application; NULL when there is none by that
 * name. */
scene_t *launcher_app_scene(const char *app_name);

/* The name of the application this scene is; NULL for the host's own. */
const char *launcher_app_name(const scene_t *scene);

#ifdef __cplusplus
}
#endif
