/* SPDX-License-Identifier: MIT */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "json_tree.h"

typedef const char *(*app_install_confirm_fn)(void);

typedef struct {
    /* Nonzero for an automatic update: the exact installed version selected. */
    uint32_t installed_id;
    const char *version;
    app_install_confirm_fn confirm_system;
    bool *retryable;
} app_install_options_t;

void app_install_start(void);
/* One package writer at a time. A named package also cannot be removed
 * through a system application while its files are being downloaded. */
bool app_install_begin(const char *app_name);
void app_install_end(void);
bool app_install_app_is_busy(const char *app_name);

bool app_install_files_are_valid(const json_t *files);
bool app_install_network_is_retryable(int status);
/* Called outside the host loop. Returns a static error, or NULL on success. */
const char *app_install_run(const char *source, int source_index, const char *app_name,
                            const json_t *files, const app_install_options_t *options);
