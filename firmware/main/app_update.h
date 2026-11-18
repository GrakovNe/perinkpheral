/* SPDX-License-Identifier: MIT */
#pragma once

void app_update_start(void);
/* In the host loop; creates a worker only for a due enabled check. */
void app_update_is_due(void);
