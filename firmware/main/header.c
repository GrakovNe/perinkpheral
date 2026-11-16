/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "header.h"

#include <stdio.h>
#include <string.h>

#include "gfx_icons.h"
#include "words.h"

_Static_assert(HOST_HEADER_LEFT == 0 && HOST_HEADER_RIGHT == 1, "the sides are places in a table");

bool header_keep(header_kept_t *kept, uint32_t app_id, int side, int icon, const char *text)
{
    if (side != HOST_HEADER_LEFT && side != HOST_HEADER_RIGHT) {
        return false;
    }
    if (icon == HEADER_NO_ICON && text[0] == '\0') {
        if (kept->side[side].app_id != app_id) {
            return false; /* not its to give back */
        }
        kept->side[side].app_id = 0;
        return true;
    }
    kept->side[side].app_id = app_id;
    kept->side[side].icon = icon;
    snprintf(kept->side[side].text, sizeof kept->side[side].text, "%s", text);
    return true;
}

bool header_keep_only(header_kept_t *kept, const uint32_t *app_ids, int count)
{
    bool changed = false;
    for (int side = 0; side < HEADER_SIDES; side++) {
        if (kept->side[side].app_id == 0) {
            continue;
        }
        bool is_there = false;
        for (int i = 0; i < count && !is_there; i++) {
            is_there = app_ids[i] == kept->side[side].app_id;
        }
        if (!is_there) {
            kept->side[side].app_id = 0;
            changed = true;
        }
    }
    return changed;
}

void header_when(const struct tm *local, bool shows_clock, char *out, size_t size)
{
    if (shows_clock) {
        snprintf(out, size, "%02d:%02d", local->tm_hour, local->tm_min);
        return;
    }
    snprintf(out, size, "%s %d %s", word((word_t)(WORD_SUN + local->tm_wday)), local->tm_mday,
             word((word_t)(WORD_JAN + local->tm_mon)));
}

screens_header_t header_compose(const header_kept_t *kept, const header_now_t *now, char *when,
                                size_t when_size)
{
    when[0] = '\0';
    if (now->time_known) {
        header_when(&now->local, now->shows_clock, when, when_size);
    }
    screens_header_t header = {
        .left_icon = now->network_ok ? GFX_ICON_SIGNAL : GFX_ICON_NO_SIGNAL,
        .left = now->scene_left,
        .right = now->scene_right != NULL ? now->scene_right : now->time_known ? when : NULL,
        .right_icon = SCREENS_NO_ICON,
    };
    if (kept->side[HOST_HEADER_LEFT].app_id != 0) {
        header.left_icon = kept->side[HOST_HEADER_LEFT].icon;
        header.left = kept->side[HOST_HEADER_LEFT].text;
    }
    if (kept->side[HOST_HEADER_RIGHT].app_id != 0) {
        header.right_icon = kept->side[HOST_HEADER_RIGHT].icon;
        header.right = kept->side[HOST_HEADER_RIGHT].text;
    }
    return header;
}
