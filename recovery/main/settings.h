// SPDX-License-Identifier: MIT
#pragma once
#include "appconf.h"
/* Read only: recovery must remain available without repairing or erasing NVS. */
void recovery_settings_read(appconf_t *settings);
