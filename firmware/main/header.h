/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>

#include "host_api.h"
#include "screens.h"

#ifdef __cplusplus
extern "C" {
#endif

/* What the header says, put together the one way the device and the
 * simulator both draw it. Pure: no clock, no network, no settings - the
 * caller hands in what they are now.
 *
 * Each side is, in this order: what an application keeps there
 * (header_set_status), what the scene on the screen gave (header_set_text), the
 * host's own - the network's icon on the left, the date or the time on the
 * right. */

#define HEADER_SIDES   2
#define HEADER_NO_ICON SCREENS_NO_ICON

/* What applications keep in the header while they are away: on each side
 * an icon and a line, the application's that set it last. */
typedef struct {
    struct {
        uint32_t app_id; /* 0: nobody has this side */
        int      icon;
        char     text[HOST_STATUS_TEXT_MAX + 1];
    } side[HEADER_SIDES];
} header_kept_t;

/* An application sets its side, or gives it back with no icon and no text
 * - only its own. True when the header is another now. */
bool header_keep(header_kept_t *kept, uint32_t app_id, int side, int icon, const char *text);

/* The applications there are now: a side kept by one that is not among
 * them is given back. True when the header is another now. */
bool header_keep_only(header_kept_t *kept, const uint32_t *app_ids, int count);

/* The host's own part. */
typedef struct {
    bool       network_ok;  /* the last use of the network worked */
    bool       time_known;  /* false: no date at all */
    bool       shows_clock; /* the time where the date stands */
    struct tm  local;       /* the user's local time */
    const char *scene_left; /* what the scene gave; NULL: nothing */
    const char *scene_right;
} header_now_t;

/* "Fri 13 Nov" in the host's words, or "15:04" when the clock is shown. */
#define HEADER_WHEN_MAX 31 /* in letters of two bytes too */
void header_when(const struct tm *local, bool shows_clock, char *out, size_t size);

/* The header to draw. `when` is room for the host's own right side, at
 * least HEADER_WHEN_MAX + 1; the result points into it and into `kept`. */
screens_header_t header_compose(const header_kept_t *kept, const header_now_t *now, char *when,
                                size_t when_size);

#ifdef __cplusplus
}
#endif
