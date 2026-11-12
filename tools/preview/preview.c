/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 *
 * Renders the host's screens on the host and writes them as PBM (P4),
 * one file per screen: what the panel would show, before flashing.
 * Built and run by tools/preview/preview.py.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "gfx.h"
#include "gfx_icons.h"
#include "screens.h"
#include "wm.h"

#define W 400
#define H 300

static uint8_t fb[W / 8 * H];
static gfx_t g;

/* P4 is 1 = black; the framebuffer is 1 = white. */
static int write_pbm(const char *path)
{
    FILE *f = fopen(path, "wb");
    if (f == NULL) {
        perror(path);
        return 1;
    }
    fprintf(f, "P4\n%d %d\n", W, H);
    for (size_t i = 0; i < sizeof fb; i++) {
        fputc((uint8_t)~fb[i], f);
    }
    fclose(f);
    return 0;
}

/* A small clock, the way an application's manifest would bring it. */
static const uint16_t CLOCK_ICON[SCREENS_ICON_CELLS] = {
    0x0F8, 0x104, 0x222, 0x421, 0x421, 0x439, 0x401, 0x401, 0x202, 0x104, 0x0F8,
};

#define ICONS_IN_A_ROW       4
#define ICON_SHEET_MARGIN    16
#define ICON_SHEET_STEP_X    96
#define ICON_SHEET_STEP_Y    50
#define ICON_SHEET_PAIR_GAP  32

static void row_from_array(void *context, int index, screens_menu_row_t *row)
{
    row->title = ((const char *const *)context)[index];
    row->icon = index == 0 ? CLOCK_ICON : NULL; /* the host's own pages have none */
}

static int save(const char *dir, const char *name)
{
    char path[512];
    snprintf(path, sizeof path, "%s/%s.pbm", dir, name);
    return write_pbm(path);
}

/* A cleared screen with the header drawn; `content` is what is left for
 * the view, the way the window manager hands it to a scene. */
static void start_screen(const screens_header_t *header, gfx_t *content)
{
    gfx_t header_surface;
    gfx_clear(&g, false);
    wm_split(&g, &header_surface, content);
    screens_header(&header_surface, header);
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: preview <out-dir>\n");
        return 2;
    }
    const char *dir = argv[1];
    gfx_init(&g, fb, W, H);

    const screens_header_t online = { true, NULL, "Wed 11 Nov" };
    /* As the device shows it before there is a network: no Wi-Fi, no date. */
    const screens_header_t offline = { false, NULL, NULL };
    gfx_t content;

    const char *titles[] = { "Clock", "Hello", "Device", "About" };
    start_screen(&online, &content);
    screens_menu(&content, row_from_array, titles, 4, 0);
    if (save(dir, "menu")) {
        return 1;
    }
    start_screen(&offline, &content);
    screens_menu(&content, row_from_array, titles, 4, 1);
    if (save(dir, "menu-offline")) {
        return 1;
    }

    /* A question from the network, as it waits for an answer and as it is
     * about to be confirmed. The question takes the whole header. */
    const screens_header_t asking = { true, "Update firmware?", " " };
    const screens_question_t unanswered = { "Cancel", "Update", false };
    start_screen(&asking, &content);
    screens_question(&content, &unanswered);
    if (save(dir, "question")) {
        return 1;
    }
    const screens_question_t chosen = { "Cancel", "Update", true };
    start_screen(&asking, &content);
    screens_question(&content, &chosen);
    if (save(dir, "question-chosen")) {
        return 1;
    }

    /* The host's icons: each as it stands beside text, and twice as large. */
    start_screen(&online, &content);
    for (int icon = 0; icon < GFX_ICON_COUNT; icon++) {
        int column = icon % ICONS_IN_A_ROW, row = icon / ICONS_IN_A_ROW;
        int left = ICON_SHEET_MARGIN + column * ICON_SHEET_STEP_X;
        int top = ICON_SHEET_MARGIN + row * ICON_SHEET_STEP_Y;
        gfx_icon(&content, (gfx_icon_id_t)icon, left, top + GFX_ICON_CELLS, 2, true);
        gfx_icon(&content, (gfx_icon_id_t)icon, left + ICON_SHEET_PAIR_GAP, top, 4, true);
    }
    if (save(dir, "icons")) {
        return 1;
    }

    return 0;
}
