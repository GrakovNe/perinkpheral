/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "scenes.h"

#include <inttypes.h>
#include <stdio.h>

#include "screens.h"
#include "sysinfo.h"
#include "wifi.h"

#define MINUTES_PER_HOUR 60

static void format_uptime(uint32_t minutes, char *text, size_t text_size)
{
    if (minutes >= MINUTES_PER_HOUR) {
        snprintf(text, text_size, "%" PRIu32 " h %" PRIu32 " min", minutes / MINUTES_PER_HOUR,
                 minutes % MINUTES_PER_HOUR);
    } else {
        snprintf(text, text_size, "%" PRIu32 " min", minutes);
    }
}

static scene_reaction_t device_button(scene_t *scene, scene_button_t button, scene_press_t press)
{
    (void)scene;
    scene_reaction_t reaction = { SCENE_IGNORED, NULL };
    if (press != SCENE_PRESS_SHORT) {
        return reaction;
    }
    if (button == SCENE_BUTTON_BACK) {
        reaction.kind = SCENE_BACK;
    } else if (button == SCENE_BUTTON_OK) {
        /* Memory and uptime are as of the last draw: OK reads them again. */
        reaction.kind = SCENE_REDRAW;
    }
    return reaction;
}

static void device_draw(scene_t *scene, gfx_t *surface)
{
    (void)scene;
    sysinfo_t info;
    sysinfo_read(&info);

    char free_memory[16], uptime[24];
    snprintf(free_memory, sizeof free_memory, "%" PRIu32 " KB", info.free_memory_kb);
    format_uptime(info.uptime_minutes, uptime, sizeof uptime);

    wifi_status_t wifi;
    wifi_get_status(&wifi);
    /* On its network, or still waiting to be told one: then it is found on
     * its own access point, which carries the device's name. */
    bool on_network = wifi.phase == WIFI_PHASE_ONLINE;
    const char *network = on_network                           ? wifi.ssid
                          : wifi.phase == WIFI_PHASE_CONNECTING ? "connecting"
                                                                : "setup mode";

    const screens_field_t fields[] = {
        { "Name", info.device_name },
        { "Wi-Fi", network },
        { "Address", on_network ? wifi.address : WIFI_SETUP_ADDRESS },
        { "Firmware", info.firmware_version },
        { "Slot", info.running_slot },
        { "Free memory", free_memory },
        { "Uptime", uptime },
    };
    screens_fields(surface, fields, (int)(sizeof fields / sizeof fields[0]));
}

static const scene_ops_t DEVICE_OPS = { .button = device_button, .draw = device_draw };

scene_t *scene_device(void)
{
    static scene_t device = { .ops = &DEVICE_OPS, .fullscreen = false };
    return &device;
}
