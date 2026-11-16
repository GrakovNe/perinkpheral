/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "app_delivery.h"
#include "gfx.h"
#include "header.h"
#include "scene.h"
#include "wm.h"

#ifdef __cplusplus
extern "C" {
#endif

/* What the parts of the host loop share, and nobody else: host.c (the
 * loop, the frame, the buttons, the deadlines, stopping), host_notices.c
 * (the question, the notices, the work notice) and host_apps.c (the list
 * of applications, deliveries, news, the header's status). All of it in
 * the host loop. */

/* What is on the screen, and whose answer is being taken off it. */
extern wm_t                 host_wm;
extern app_delivery_state_t host_delivery;

/* ---- host.c ---- */

/* An application taking an answer off the screen, piece by piece, gives
 * way: the rest of that answer is dropped and its poll asked again. */
void host_end_background_answer(void);

/* The scene whose picture the panel shows; read from other tasks too. */
const scene_t *host_scene_on_panel(void);

/* The menu and the splash made anew for the list of applications as it
 * is now. */
void host_renew_menu(void);

/* ---- host_notices.c ---- */

void host_notices_start(void);
/* After every event: an answered question and a dismissed notice leave,
 * and the next notice is shown. Never from inside a scene's handler. */
void host_notices_after_event(void);
void host_confirm_timed_out(void);

/* ---- host_apps.c ---- */

/* While an answer is handed to an application that is not on the screen. */
bool host_is_delivering(void);
/* What applications keep in the header while they are away. */
const header_kept_t *host_header_kept(void);
/* An alarm that has come, for its application. */
void host_ring_alarm(uint32_t app_id, int alarm_number);
/* News of a HOST_INTENT_* for whoever asked for it. */
void host_tell_news(int intent);
/* After every event: the list read anew if a handler asked for it. */
void host_apps_after_event(void);

/* A text into a field of the host's: as much of it as fits, cut between
 * characters, never inside one. */
#define HOST_COPY_TEXT(field, text) gfx_text_copy((field), sizeof(field), (text))

#ifdef __cplusplus
}
#endif
