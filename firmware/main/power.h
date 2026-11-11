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

/* Once, after Wi-Fi has started. */
void power_start(bool is_saving);

/* The setting changed. From any task. */
void power_set_saving(bool is_saving);
bool power_is_saving(void);

/* Somebody needs the network until the matching release: counted. In the
 * plugged profile both do nothing. From any task. */
void power_hold_radio(void);
void power_release_radio(void);

/* A button was pressed: the user is here. From any task. */
void power_note_press(void);

#ifdef __cplusplus
}
#endif
