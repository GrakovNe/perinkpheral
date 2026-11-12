/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* A part of a 1-bit frame, in what the panel's controller counts in:
 * whole bytes across - eight pixels - and rows down. Empty when it has
 * no rows. */
typedef struct {
    int column;  /* the first byte of a row */
    int row;     /* the first row */
    int columns; /* bytes in a row */
    int rows;
} frame_box_t;

bool frame_box_is_empty(const frame_box_t *box);

/* The smallest box outside which two frames of `columns` bytes by `rows`
 * rows are the same; empty when they are the same everywhere. */
frame_box_t frame_box_of_change(const uint8_t *frame, const uint8_t *other, int columns, int rows);

/* The smallest box that holds both. */
frame_box_t frame_box_join(const frame_box_t *box, const frame_box_t *other);

#ifdef __cplusplus
}
#endif
