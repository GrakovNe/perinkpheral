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

/* Five applications from the catalog, a few more a developer uploaded, and
 * the system ones. */
#define LAUNCHER_ENTRIES_MAX 11

/* Reads the store and builds the scenes; once, at start. */
void launcher_start(void);

/* A file of a system application - its words in another language, which
 * are part of the firmware - or NULL. */
const uint8_t *launcher_system_file(const char *app_name, const char *file, size_t *size);

/* The store changed: the scenes of the installed applications are made
 * anew. `drop` is called for every old one before it is freed, for whoever
 * holds its pointer; the system applications stay as they are. Only in
 * the host loop, with the poller held (poller_hold()). */
typedef void (*launcher_drop_fn)(scene_t *scene, void *context);
void launcher_reload(launcher_drop_fn drop, void *context);

/* The About application, which is also what the splash shows; NULL when
 * the firmware was built without a sound one. */
scene_t *launcher_about(void);

/* The screen that says how to give the device a network; NULL like the
 * one above. */
scene_t *launcher_setup(void);

/* Any scene there is by its application's name, a system one ("sys.about")
 * included; for the development tools. NULL when there is none. */
scene_t *launcher_scene(const char *name);

/* What the menu lists: applications by title, then the host's pages. */
const menu_entry_t *launcher_entries(int *count);

/* Takes an installed application off the device: its files and its code,
 * and - `forgets` - what it stored, which otherwise waits for it to come
 * back. False when the files could not be removed; nothing else is
 * touched then. Only in the host loop, and the list is to be read anew
 * after it (host_reload_apps(), or EVENT_APPS_CHANGED from inside an
 * application's handler). */
bool launcher_remove(const char *app_name, bool forgets);

/* How many applications the launcher has room for. */
#define LAUNCHER_INSTALLED_MAX (LAUNCHER_ENTRIES_MAX - 3)

/* The installed applications as they are now, copied: for any task but
 * the host loop, never waits. A reader overtaken by the list being read
 * anew copies again. Returns how many; `capacity` is at least
 * LAUNCHER_INSTALLED_MAX. */
int launcher_read_installed(app_manifest_t *copy, int capacity);

/* The installed applications that can be started, as their manifests say.
 * The list itself: only for the host loop, and for the poller while it is
 * held. */
const app_manifest_t *launcher_installed(int *count);

/* The scene of an installed application; NULL when there is none by that
 * name. */
scene_t *launcher_app_scene(const char *app_name);

/* The name of the application this scene is; NULL for the host's own. */
const char *launcher_app_name(const scene_t *scene);

#ifdef __cplusplus
}
#endif
