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
#include "screens.h"

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

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: preview <out-dir>\n");
        return 2;
    }
    char path[512];
    gfx_init(&g, fb, W, H);

    screens_splash(&g);
    snprintf(path, sizeof path, "%s/splash.pbm", argv[1]);
    if (write_pbm(path)) {
        return 1;
    }

    const uint32_t counts[4] = { 12, 3, 0, 1048 };
    screens_counters(&g, counts);
    snprintf(path, sizeof path, "%s/counters.pbm", argv[1]);
    return write_pbm(path);
}
