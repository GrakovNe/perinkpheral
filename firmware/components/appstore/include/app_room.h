/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* What room there is for applications, with no flash in sight: where a
 * module goes in the partition of code, and whether the files promised
 * to the applications that write still fit. Pure, host-tested. */

/* ---- the code: modules in whole sectors, each in one piece ---- */

/* A module in the partition: its first sector and how many it takes. */
typedef struct {
    int start;
    int sectors;
} app_room_extent_t;

/* The first sector of the lowest free run of `needed` sectors in a
 * partition of `total`, the extents in it sorted by start; -1 when there
 * is no such run. */
int app_room_first_fit(const app_room_extent_t *taken, int count, int total, int needed);

/* How many sectors no extent takes. */
int app_room_free(const app_room_extent_t *taken, int count, int total);

/* The first extent with free sectors below it, which moving down would
 * close; -1 when they lie packed from the start. */
int app_room_to_move(const app_room_extent_t *taken, int count);

/* ---- the files: what is kept free for the applications that write ---- */

/* What the file system of the applications needs free to work in. */
#define APP_ROOM_FILES_SPARE_BYTES (13 * 4096)

typedef struct {
    bool writes_files; /* "writes_files": it has HOST_FILES_WRITER_MAX */
    long files_bytes;  /* what its data files take now, in whole blocks */
} app_room_files_t;

/* What has to stay free for them: each one that writes, the rest of its
 * HOST_FILES_WRITER_MAX. */
long app_room_promised(const app_room_files_t *apps, int count);

#ifdef __cplusplus
}
#endif
