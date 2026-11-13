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

/* Rectangle of a 1-bit frame in controller units: bytes (8 pixels)
 * horizontally, rows vertically. Empty when `rows` is 0. */
typedef struct {
    int column;  /* first byte of a row */
    int row;     /* first row */
    int columns; /* bytes in a row */
    int rows;
} frame_box_t;

bool frame_box_is_empty(const frame_box_t *box);

/* Smallest box that contains every difference between two frames of
 * `columns` bytes by `rows` rows. Empty when the frames are equal. */
frame_box_t frame_box_of_change(const uint8_t *frame, const uint8_t *other, int columns, int rows);

/* Smallest box that contains both boxes. */
frame_box_t frame_box_join(const frame_box_t *box, const frame_box_t *other);

#ifdef __cplusplus
}
#endif
