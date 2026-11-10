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

    const screens_menu_item_t items[] = {
        { "Device", "name, firmware" },
        { "About", "logo, license" },
    };
    const screens_header_t online = { true, "Wed 11 Nov" };
    screens_menu(&g, items, 2, 0, &online);
    snprintf(path, sizeof path, "%s/menu.pbm", argv[1]);
    if (write_pbm(path)) {
        return 1;
    }

    /* As the device shows it before there is a network: no Wi-Fi, no date. */
    const screens_header_t offline = { false, NULL };
    screens_menu(&g, items, 2, 1, &offline);
    snprintf(path, sizeof path, "%s/menu-offline.pbm", argv[1]);
    if (write_pbm(path)) {
        return 1;
    }

    const screens_field_t fields[] = {
        { "Name", "Perinkpheral-1A2B" },
        { "Firmware", "v0.1.0" },
        { "ESP-IDF", "v5.5.5" },
        { "Slot", "ota_0" },
        { "Free memory", "412 KB" },
        { "Uptime", "3 h 12 min" },
    };
    screens_fields(&g, fields, 6, &online);
    snprintf(path, sizeof path, "%s/device.pbm", argv[1]);
    return write_pbm(path);
}
