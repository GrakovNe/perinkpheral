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

static void item_from_array(void *context, int index, screens_menu_item_t *item)
{
    *item = ((const screens_menu_item_t *)context)[index];
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

    gfx_clear(&g, false);
    screens_splash(&g);
    if (save(dir, "splash")) {
        return 1;
    }

    const screens_header_t online = { true, "Wed 11 Nov" };
    /* As the device shows it before there is a network: no Wi-Fi, no date. */
    const screens_header_t offline = { false, NULL };
    gfx_t content;

    screens_menu_item_t items[] = {
        { "Device", "name, firmware" },
        { "About", "logo, license" },
    };
    start_screen(&online, &content);
    screens_menu(&content, item_from_array, items, 2, 0);
    if (save(dir, "menu")) {
        return 1;
    }
    start_screen(&offline, &content);
    screens_menu(&content, item_from_array, items, 2, 1);
    if (save(dir, "menu-offline")) {
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
    start_screen(&online, &content);
    screens_fields(&content, fields, 6);
    if (save(dir, "device")) {
        return 1;
    }

    start_screen(&offline, &content);
    screens_setup(&content, "Perinkpheral-1A2B", "192.168.4.1");
    return save(dir, "setup");
}
