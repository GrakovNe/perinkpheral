/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#pragma once

#include <stdbool.h>

#include "gfx.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Scene: the owner of the screen and the buttons - the menu, a host page or
 * an application. This interface is all the window manager uses.
 * A scene does not access the panel: it reports a changed picture and draws
 * on request. */

#define SCENE_BUTTON_COUNT 4

/* Button layout of the host. Recommended for applications. */
typedef enum {
    SCENE_BUTTON_UP,
    SCENE_BUTTON_OK,
    SCENE_BUTTON_DOWN,
    SCENE_BUTTON_BACK,
} scene_button_t;

/* Press events. Raw: DOWN, UP. Interpreted: SHORT, LONG.
 * Order of a short press: DOWN, UP, SHORT. Order of a long press: DOWN, LONG, UP. */
typedef enum {
    SCENE_PRESS_DOWN,
    SCENE_PRESS_UP,
    SCENE_PRESS_SHORT,
    SCENE_PRESS_LONG,
} scene_press_t;

typedef struct scene scene_t;

typedef enum {
    SCENE_IGNORED, /* no change */
    SCENE_REDRAW,  /* picture changed; shown by the refresh policy */
    SCENE_REDRAW_SOON,  /* picture changed; shown at once, as after a press */
    SCENE_REDRAW_NO_BLINK, /* picture changed; shown at once, never as a full refresh */
    SCENE_REDRAW_CLEAN, /* picture changed; shown with a full refresh */
    SCENE_OPEN,    /* push `open` above this scene; BACK there returns here */
    SCENE_REPLACE, /* close this scene and show `open` in its place */
    SCENE_BACK,    /* close this scene: one step down the stack */
} scene_reaction_kind_t;

typedef struct {
    scene_reaction_kind_t kind;
    scene_t              *open; /* SCENE_OPEN and SCENE_REPLACE only */
} scene_reaction_t;

typedef struct {
    /* Optional. enter: the scene received the screen. leave: it is about to lose it. */
    void (*enter)(scene_t *scene);
    void (*leave)(scene_t *scene);

    scene_reaction_t (*button)(scene_t *scene, scene_button_t button, scene_press_t press);

    /* Optional. A timer set by the scene expired. */
    scene_reaction_t (*timer)(scene_t *scene, int timer_id);

    /* Draws the whole picture. `surface` is the screen or the area under
     * the header, cleared before the call. */
    void (*draw)(scene_t *scene, gfx_t *surface);
} scene_ops_t;

struct scene {
    const scene_ops_t *ops;
    bool               fullscreen; /* false: the host header is drawn above the scene */
    /* A press here is irreversible. The scene receives only presses that
     * began after its picture reached the panel. */
    bool               asks_once;
    /* Header texts of the scene. NULL: the side shows the host's content. */
    const char        *header_left;
    const char        *header_right;
};

#ifdef __cplusplus
}
#endif
