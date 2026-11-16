/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#pragma once

#include <stdint.h>

#include "esp_err.h"
#include "appconf.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The only module that touches the host's own settings in NVS (an
 * application's store is app_kv.c). */
void settings_nvs_init(void);

/* What the host keeps for an installed application, in its own NVS rather
 * than the applications' stores: those are the applications' to fill to
 * the last byte promised. */
#define SETTINGS_APP_POLLS 4
typedef struct {
    uint32_t seen;                       /* the id of the version that last ran; 0: none */
    uint32_t pace[SETTINGS_APP_POLLS];   /* seconds a poll asked for; 0: the manifest's */
} settings_app_record_t;

/* Zero when nothing is kept. */
void settings_load_app_record(const char *app_name, settings_app_record_t *record);
void settings_save_app_record(const char *app_name, const settings_app_record_t *record);
/* Removed: nothing of it stays. */
void settings_forget_app_record(const char *app_name);

/* The host's settings have one owner, this module, and one place they are
 * changed from, the host loop. Everybody else reads a copy.
 *
 * Reads what is stored - defaults when nothing is, or when what is stored
 * does not validate - and makes it the settings in effect. Once, before
 * anything asks for them. */
void settings_start(const char device_id[APPCONF_DEVICE_ID_SIZE]);

/* The settings as they are now, copied whole: from any task, never waits.
 * A reader that was overtaken by a change copies again, so the copy is
 * always one that was in effect, if not the newest. */
void settings_read(appconf_t *copy);

typedef enum {
    SETTINGS_CHANGED,
    SETTINGS_INVALID,    /* `problem` says what is wrong */
    SETTINGS_NOT_STORED, /* the storage failed: nothing changed */
} settings_outcome_t;

/* Checks `wanted` and stores it, all or nothing, and tells whoever
 * follows the settings. ONLY in the host loop (events_call() from
 * elsewhere). `needs_restart` may be NULL: true when something changed that
 * is taken at start - the name, the time server, the network. */
settings_outcome_t settings_change(const appconf_t *wanted, char *problem, size_t problem_size,
                                   bool *needs_restart);

/* One setting by its key (appconf_fields()), its value as text: what the
 * page and the system API set. The network is not set this way: it is
 * asked about on the device. ONLY in the host loop. */
settings_outcome_t settings_set(const char *key, size_t key_length, const char *value,
                                char *problem, size_t problem_size, bool *needs_restart);

/* Whoever puts a setting into effect - the clock, the panel, the power
 * profile, the header - follows the settings: told in the host loop after
 * every change, with the settings before it and after. The store knows
 * nothing of what a setting is for. Before the host loop runs; called in
 * the order they asked. */
typedef void (*settings_watch_fn)(const appconf_t *before, const appconf_t *now);
void settings_watch(settings_watch_fn watcher);

/* The application that was on the screen last, to return to after a
 * restart. name_size is at least 16; the name is empty when there was
 * none. Saving the name that is stored already writes nothing. */
void settings_load_last_app(char *name, size_t name_size);
void settings_save_last_app(const char *name);

/* Where applications are installed from: the addresses of the catalogs,
 * a line each (see catalog.h). Loading gives the official one first and
 * then the ones the user added; saving takes only the added ones.
 * sources_size is at least CATALOG_SOURCES_TEXT_MAX. */
void      settings_load_sources(char *sources, size_t sources_size);

/* The order of the menu the user set, the names a line each (menu_order.h);
 * empty when none was. order_size is at least MENU_ORDER_MAX. */
void      settings_load_menu_order(char *order, size_t order_size);
esp_err_t settings_save_menu_order(const char *order);
esp_err_t settings_save_sources(const char *added_sources);

#ifdef __cplusplus
}
#endif
