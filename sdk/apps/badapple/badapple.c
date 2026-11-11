/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 *
 * A video on e-paper: not something to watch, a test. It asks the panel
 * for a new picture as often as the host lets it, which shows what the
 * panel does under four hundred refreshes in a row, and it draws every
 * frame as a few hundred rectangles, which shows what the interpreter
 * costs. When the video ends, the numbers are on the screen.
 *
 * The frames are in frames.h, made by tools/mkvideo: each one the runs of
 * pixels that stay and that flip since the frame before.
 */
#include <stdbool.h>

#include "app_text.h"
#include "frames.h"
#include "host_api.h"

#define FRAME_TIMER    0
#define BUTTON_OK      2
#define BUTTON_BACK    4
#define PIXELS         (VIDEO_WIDTH * VIDEO_HEIGHT)
#define MS_PER_10_S    10000
#define RUN_MORE_BIT   0x80 /* in a run's byte: the number goes on in the next one */
#define RUN_VALUE_BITS 7
#define LINE_HEIGHT    30
#define MARGIN         16

static uint8_t  picture[PIXELS]; /* 1 where there is ink */
static uint32_t next_run;        /* where in video_runs the next frame starts */
static int32_t  decoded_frames;  /* the picture is frame number decoded_frames - 1 */
static int64_t  started_ms;
static bool     is_over;

/* What the test measures. */
static int32_t draws, rectangles, draw_ms_total, draw_ms_worst, decode_ms_total;

static uint32_t read_run(void)
{
    uint32_t length = 0;
    int32_t  shift = 0;
    uint8_t  byte;
    do {
        byte = video_runs[next_run++];
        length |= (uint32_t)(byte & ~RUN_MORE_BIT) << shift;
        shift += RUN_VALUE_BITS;
    } while (byte & RUN_MORE_BIT);
    return length;
}

/* The next frame over the one in `picture`. */
static void decode_frame(void)
{
    bool flips = false; /* a frame starts with the pixels that stay */
    for (int32_t pixel = 0; pixel < PIXELS;) {
        uint32_t length = read_run();
        if (flips) {
            for (uint32_t i = 0; i < length; i++) {
                picture[pixel + (int32_t)i] ^= 1;
            }
        }
        pixel += (int32_t)length;
        flips = !flips;
    }
    decoded_frames++;
}

static void start_over(void)
{
    for (int32_t pixel = 0; pixel < PIXELS; pixel++) {
        picture[pixel] = 0;
    }
    next_run = 0;
    decoded_frames = 0;
    is_over = false;
    draws = rectangles = draw_ms_total = draw_ms_worst = decode_ms_total = 0;
    started_ms = time_uptime_ms();
}

/* The video keeps real time: whatever the panel could not show is
 * skipped, not played late. */
static void catch_up(void)
{
    int64_t before_ms = time_uptime_ms();
    int32_t due_frame =
        (int32_t)((before_ms - started_ms) * VIDEO_FRAMES_PER_10_S / MS_PER_10_S) + 1;
    if (due_frame >= VIDEO_FRAMES) {
        due_frame = VIDEO_FRAMES;
        is_over = true;
    }
    while (decoded_frames < due_frame) {
        decode_frame();
    }
    decode_ms_total += (int32_t)(time_uptime_ms() - before_ms);
}

void app_on_focus(void)
{
    start_over();
    timer_set(FRAME_TIMER, 1);
}

void app_on_timer(int32_t id)
{
    (void)id;
    catch_up();
    /* Nobody pressed anything, and still it is wanted now. */
    request_redraw(HOST_REDRAW_SOON);
    if (!is_over) {
        timer_set(FRAME_TIMER, MS_PER_10_S / VIDEO_FRAMES_PER_10_S);
    }
}

void app_on_button(int32_t button, int32_t press)
{
    if (press != HOST_PRESS_SHORT) {
        return;
    }
    if (button == BUTTON_BACK) {
        request_exit();
    } else if (button == BUTTON_OK) {
        start_over();
        timer_set(FRAME_TIMER, 1);
    }
}

static void draw_picture(void)
{
    int32_t pixel_size = gfx_width() / VIDEO_WIDTH;
    int32_t left = (gfx_width() - VIDEO_WIDTH * pixel_size) / 2;
    int32_t top = (gfx_height() - VIDEO_HEIGHT * pixel_size) / 2;
    for (int32_t row = 0; row < VIDEO_HEIGHT; row++) {
        const uint8_t *line = &picture[row * VIDEO_WIDTH];
        /* A run of ink in a row is one rectangle. */
        for (int32_t column = 0; column < VIDEO_WIDTH;) {
            if (!line[column]) {
                column++;
                continue;
            }
            int32_t run_start = column;
            while (column < VIDEO_WIDTH && line[column]) {
                column++;
            }
            gfx_rect(left + run_start * pixel_size, top + row * pixel_size,
                     (column - run_start) * pixel_size, pixel_size, HOST_RECT_FILLED);
            rectangles++;
        }
    }
}

static void draw_line(int32_t line, const char *label, int32_t value, const char *unit)
{
    char buffer[40];
    app_text_t text;
    app_text_start(&text, buffer, sizeof buffer);
    app_text_add(&text, label);
    app_text_add_number(&text, value);
    app_text_add(&text, unit);
    gfx_text(MARGIN, MARGIN + line * LINE_HEIGHT, HOST_FONT_TEXT, buffer, app_text_length(buffer));
}

static void draw_numbers(void)
{
    int32_t shown = draws > 0 ? draws : 1;
    draw_line(0, "Frames in the video: ", VIDEO_FRAMES, "");
    draw_line(1, "Frames drawn: ", draws, "");
    draw_line(2, "Draw, average: ", draw_ms_total / shown, " ms");
    draw_line(3, "Draw, worst: ", draw_ms_worst, " ms");
    draw_line(4, "Rectangles a frame: ", rectangles / shown, "");
    draw_line(5, "Decoding, all of it: ", decode_ms_total, " ms");
    draw_line(7, "2 again, 4 back", 0, "");
}

void app_on_draw(void)
{
    if (is_over) {
        draw_numbers();
        return;
    }
    int64_t before_ms = time_uptime_ms();
    draw_picture();
    int32_t took_ms = (int32_t)(time_uptime_ms() - before_ms);
    draws++;
    draw_ms_total += took_ms;
    if (took_ms > draw_ms_worst) {
        draw_ms_worst = took_ms;
    }
}
