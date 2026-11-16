/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "app_room.h"

#include "manifest_limits.h"

int app_room_first_fit(const app_room_extent_t *taken, int count, int total, int needed)
{
    if (needed < 1) {
        return -1;
    }
    int free_from = 0;
    for (int i = 0; i <= count; i++) {
        int free_to = i < count ? taken[i].start : total;
        if (free_to - free_from >= needed) {
            return free_from;
        }
        if (i < count && taken[i].start + taken[i].sectors > free_from) {
            free_from = taken[i].start + taken[i].sectors;
        }
    }
    return -1;
}

int app_room_free(const app_room_extent_t *taken, int count, int total)
{
    int used = 0;
    for (int i = 0; i < count; i++) {
        used += taken[i].sectors;
    }
    return total - used;
}

int app_room_to_move(const app_room_extent_t *taken, int count)
{
    int packed_to = 0;
    for (int i = 0; i < count; i++) {
        if (taken[i].start > packed_to) {
            return i;
        }
        packed_to = taken[i].start + taken[i].sectors;
    }
    return -1;
}

long app_room_promised(const app_room_files_t *apps, int count)
{
    long promised = 0;
    for (int i = 0; i < count; i++) {
        long left = MANIFEST_FILES_WRITER_MAX_BYTES - apps[i].files_bytes;
        if (apps[i].writes_files && left > 0) {
            promised += left;
        }
    }
    return promised;
}
