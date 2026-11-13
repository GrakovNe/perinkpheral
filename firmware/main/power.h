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

/* The power profile. Plugged: the radio is always on and the chip never
 * sleeps. Saving - the device runs from its battery: the chip sleeps
 * whenever nothing runs (light sleep: memory and the picture are kept, a
 * timer or any button wakes it), and the radio is on only while somebody
 * needs it - a poll, or the user, who gets it for a while after a press.
 *
 * Nothing here measures anything: the profile is a setting. */

/* The least a poll may be apart while saving. */
#define POWER_SAVING_POLL_MIN_S 300

/* After a press, and after power-up, the device stays reachable this long. */
#define POWER_AWAKE_AFTER_PRESS_MS (2 * 60 * 1000)

/* `changed` is called, from whichever task it happens in, when the device
 * becomes one that a person may be looking for on the network, and when
 * it stops being one: always while plugged, the while after a press while
 * saving. Before power_start(). */
void power_on_reachable(void (*changed)(bool is_reachable));

/* What the user chose. Whether the device is saving now follows from it,
 * and with POWER_AUTO from the cable too. */
typedef enum {
    POWER_NEVER,  /* plugged in: the radio always on */
    POWER_ALWAYS, /* saving whatever it is plugged into */
    POWER_AUTO,   /* saving unless a computer is on the USB cable - the one
                     thing the board can tell: a charger looks like the battery */
} power_mode_t;

/* Once, after Wi-Fi has started. */
void power_start(power_mode_t mode);

/* The setting changed. From any task. */
void power_set_mode(power_mode_t mode);

/* DEADLINE_POWER_LOOK has come: looks at the cable. True when the device
 * began to save or stopped. In the host loop. */
bool power_look(void);

bool power_is_saving(void);

/* Somebody needs the network until the matching release: counted. In the
 * plugged profile both do nothing. From any task. */
void power_hold_radio(void);
void power_release_radio(void);

/* A button was pressed: the user is here. From any task. */
void power_note_press(void);

/* DEADLINE_AWAKE_WINDOW has come: the while after a press is over. */
void power_awake_period_ended(void);

#ifdef __cplusplus
}
#endif
