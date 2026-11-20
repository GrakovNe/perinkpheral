/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 *
 * The four buttons: their names, the actions of a press, and the handlers.
 */
#pragma once

#include "base.h"

/* Buttons, in their order on the device. Perinkpheral OS uses these meanings on
 * all of its screens. Applications should keep them. */
#define BTN_UP   1
#define BTN_OK   2
#define BTN_DOWN 3
#define BTN_BACK 4

/* Button actions. Each press produces two kinds of events:
 *   raw:         KEYDOWN, then KEYUP;
 *   interpreted: PRESSED when a short press ends, or LONG_PRESSED after
 *                600 ms while the button is still held. A long press
 *                produces no PRESSED; its release is a KEYUP only.
 * Most applications handle PRESSED and LONG_PRESSED only. */
#define BTN_KEYDOWN      0
#define BTN_KEYUP        1
#define BTN_PRESSED      2
#define BTN_LONG_PRESSED 3

/* Short press of BACK.
 * Without this export, a short press of BACK closes the application and
 * returns the user to the previous screen.
 * With this export, the press is delivered here. Typical handling: go one
 * page back; on the first page call screen_leave().
 * BTN_BACK_PRESSED is never delivered to app_on_button(). BTN_BACK_KEYDOWN
 * and BTN_BACK_KEYUP are. A long press of BACK always opens Perinkpheral OS menu. */
APP_EXPORT(app_on_back, void app_on_back(void));

/* A button event combines a button and an action. Each combination has a
 * constant:
 *
 *     void app_on_button(int32_t event)
 *     {
 *         switch (event) {
 *         case BTN_OK_PRESSED:        start();          break;
 *         case BTN_OK_LONG_PRESSED:   reset();          break;
 *         }
 *     }
 *
 * BTN_OF() and BTN_ACTION_OF() split an event into its two parts.
 * BTN_BACK_LONG_PRESSED is never delivered: a long press of BACK opens
 * Perinkpheral OS menu. */
#define BTN_EVENT(button, action) ((button) << 2 | (action))
#define BTN_OF(event)             ((event) >> 2)
#define BTN_ACTION_OF(event)      ((event) & 3)

#define BTN_UP_KEYDOWN        BTN_EVENT(BTN_UP, BTN_KEYDOWN)
#define BTN_UP_KEYUP          BTN_EVENT(BTN_UP, BTN_KEYUP)
#define BTN_UP_PRESSED        BTN_EVENT(BTN_UP, BTN_PRESSED)
#define BTN_UP_LONG_PRESSED   BTN_EVENT(BTN_UP, BTN_LONG_PRESSED)
#define BTN_OK_KEYDOWN        BTN_EVENT(BTN_OK, BTN_KEYDOWN)
#define BTN_OK_KEYUP          BTN_EVENT(BTN_OK, BTN_KEYUP)
#define BTN_OK_PRESSED        BTN_EVENT(BTN_OK, BTN_PRESSED)
#define BTN_OK_LONG_PRESSED   BTN_EVENT(BTN_OK, BTN_LONG_PRESSED)
#define BTN_DOWN_KEYDOWN      BTN_EVENT(BTN_DOWN, BTN_KEYDOWN)
#define BTN_DOWN_KEYUP        BTN_EVENT(BTN_DOWN, BTN_KEYUP)
#define BTN_DOWN_PRESSED      BTN_EVENT(BTN_DOWN, BTN_PRESSED)
#define BTN_DOWN_LONG_PRESSED BTN_EVENT(BTN_DOWN, BTN_LONG_PRESSED)
#define BTN_BACK_KEYDOWN      BTN_EVENT(BTN_BACK, BTN_KEYDOWN)
#define BTN_BACK_KEYUP        BTN_EVENT(BTN_BACK, BTN_KEYUP)
#define BTN_BACK_PRESSED      BTN_EVENT(BTN_BACK, BTN_PRESSED)
#define BTN_BACK_LONG_PRESSED BTN_EVENT(BTN_BACK, BTN_LONG_PRESSED)

APP_EXPORT(app_on_button, void app_on_button(int32_t event));
