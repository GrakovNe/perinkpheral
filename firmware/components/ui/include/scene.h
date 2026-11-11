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

/* A scene is whatever owns the screen and the buttons at the moment: the
 * menu, a host page, an application. The window manager knows scenes only
 * through this interface. A scene never touches the panel: it says that
 * its picture changed and draws when asked. */

#define SCENE_BUTTON_COUNT 4

/* The host's layout, which applications are encouraged to keep. */
typedef enum {
    SCENE_BUTTON_UP,
    SCENE_BUTTON_OK,
    SCENE_BUTTON_DOWN,
    SCENE_BUTTON_BACK,
} scene_button_t;

/* A press arrives as it happens (down, up) and as what it meant (short,
 * long): down, up, short - or down, long, up. A scene that only wants the
 * meaning ignores the first two. */
typedef enum {
    SCENE_PRESS_DOWN,
    SCENE_PRESS_UP,
    SCENE_PRESS_SHORT,
    SCENE_PRESS_LONG,
} scene_press_t;

typedef struct scene scene_t;

typedef enum {
    SCENE_IGNORED, /* nothing changed */
    SCENE_REDRAW,  /* the picture changed */
    SCENE_REDRAW_SOON,  /* it changed and should be seen now, though nobody pressed anything */
    SCENE_REDRAW_NO_BLINK, /* it changed, should be seen now, and one of a row: no full refresh */
    SCENE_REDRAW_CLEAN, /* it changed and should be shown without traces of the old one */
    SCENE_OPEN,    /* give the screen to another scene */
    SCENE_BACK,    /* done here: return to where the user came from */
} scene_reaction_kind_t;

typedef struct {
    scene_reaction_kind_t kind;
    scene_t              *open; /* for SCENE_OPEN */
} scene_reaction_t;

typedef struct {
    /* Optional: the scene got the screen / is about to lose it. */
    void (*enter)(scene_t *scene);
    void (*leave)(scene_t *scene);

    scene_reaction_t (*button)(scene_t *scene, scene_button_t button, scene_press_t press);

    /* Optional: a timer the scene set ran out. */
    scene_reaction_t (*timer)(scene_t *scene, int timer_id);

    /* Draws the whole picture into the surface it is given: the screen,
     * or the area under the header. The surface arrives cleared. */
    void (*draw)(scene_t *scene, gfx_t *surface);
} scene_ops_t;

struct scene {
    const scene_ops_t *ops;
    bool               fullscreen; /* false: the host draws its header above */
};

#ifdef __cplusplus
}
#endif
