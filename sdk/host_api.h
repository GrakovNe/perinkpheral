/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 *
 * perINKpheral host API: the contract between the host firmware and wasm
 * applications. Applications include this header and link the imports.
 * The host builds its import table from the same names.
 *
 * The API is not frozen before the first release and is not versioned
 * until then. A change requires rebuilding all applications.
 * HOST_API_VERSION and the "abi" field of a manifest stay at 2 until the
 * first release.
 */
#pragma once

#include <stdint.h>

#define HOST_API_VERSION 2

/* The host firmware includes this header for its constants. Outside a
 * wasm build the declarations expand to nothing, because the host defines
 * functions with some of the same names. */
#ifdef __wasm__
#define HOST_IMPORT(name, ...) \
    __attribute__((import_module("host"), import_name(#name))) __VA_ARGS__
#define APP_EXPORT(name, ...) __attribute__((export_name(#name))) __VA_ARGS__
#else
#define HOST_IMPORT(name, ...)
#define APP_EXPORT(name, ...)
#endif

/* Return codes of imports that return no value of their own. */
#define HOST_OK               0
#define HOST_ERR_NOT_DRAWING -1 /* drawing call outside app_on_draw() */
#define HOST_ERR_ARGUMENT    -2 /* pointer outside the application memory; unknown font or timer */
#define HOST_ERR_NOT_FOUND   -3 /* kv_get: key not stored; time_local: time not set */
#define HOST_ERR_NO_SPACE    -4 /* kv_get: value larger than the buffer; kv_set: store full */
#define HOST_ERR_FAILED      -5 /* storage failure; nothing was read or written */

/* ---- Exports (all optional) ---- */

/* Called once, immediately after the module is loaded.
 * The host unloads an application every time it leaves the screen. State
 * that must persist is kept with kv_* calls, not in global variables. */
APP_EXPORT(app_init, void app_init(void));

/* app_on_focus: the application has received the screen.
 * app_on_blur: the application is about to lose the screen. */
APP_EXPORT(app_on_focus, void app_on_focus(void));
APP_EXPORT(app_on_blur, void app_on_blur(void));

/* The last call of an instance, before it is unloaded for any reason:
 * leaving the screen, end of a background delivery, device restart or
 * sleep, removal of the application. Save persistent state here.
 * app_on_blur() is called first if the application was on the screen. */
APP_EXPORT(app_on_terminate, void app_on_terminate(void));

/* Buttons, in their order on the device. The host uses these meanings on
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
 * page back; on the first page call request_exit().
 * BTN_BACK_PRESSED is never delivered to app_on_button(). BTN_BACK_KEYDOWN
 * and BTN_BACK_KEYUP are. A long press of BACK always opens the host menu. */
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
 * the host menu. */
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

/* A timer set with timer_set() has expired. */
APP_EXPORT(app_on_timer, void app_on_timer(int32_t id));

/* One piece of the answer to a poll or to http_request(). The host does
 * not hold a complete response in memory.
 *
 * request:     poll number in the manifest, or the tag of http_request().
 * data:        the piece, in the buffer returned by app_data_buffer().
 *              Valid until the handler returns.
 * flags:       HOST_DATA_FIRST on the first piece, HOST_DATA_LAST on the
 *              last one. A short answer is one piece with both flags.
 *              HOST_DATA_FAILED: single call without data; the request
 *              was not made or was interrupted.
 * http_status: HTTP status, delivered with the first piece. With
 *              HOST_DATA_FAILED: a HOST_FAILED_* code.
 *
 * Delivery can stop without HOST_DATA_LAST and without HOST_DATA_FAILED:
 * a background instance is unloaded when the user presses a button. Only
 * app_on_terminate() is called then. The next HOST_DATA_FIRST starts a
 * new answer. The host repeats an interrupted poll after one minute.
 * Discard partially read data.
 *
 * Called on and off the screen. Off the screen the application is loaded
 * for the delivery and unloaded after it. Save the result to the store.
 * Call request_focus() to request the screen. */
#define HOST_DATA_FIRST     1
#define HOST_DATA_LAST      2
#define HOST_DATA_FAILED    4
#define HOST_DATA_CHUNK_MAX 1024
/* Failure codes, delivered in place of the HTTP status: in app_on_data()
 * with HOST_DATA_FAILED, and in app_on_values(). */
#define HOST_FAILED_NO_NETWORK  (-1) /* no network connection */
#define HOST_FAILED_NO_ANSWER   (-2) /* no answer from the server, or an incomplete answer */
#define HOST_FAILED_NOT_READY   (-3) /* a {{setting}} used by the request is empty */
#define HOST_FAILED_NOT_ALLOWED (-4) /* redirect to a host outside the manifest "hosts" list */
#define HOST_FAILED_TIMEOUT     (-5) /* timeout after the server was reached */
#define HOST_FAILED_NOT_TRUSTED (-6) /* HTTPS certificate not trusted: self-signed, expired,
                                        or issued for another name */
#define HOST_FAILED_TOO_LARGE   (-7) /* answer larger than HOST_ANSWER_MAX, or larger than
                                        HOST_ANSWER_PIECES_MAX when streamed */
#define HOST_FAILED_NO_MEMORY   (-8) /* insufficient device memory for the request */
/* Maximum answer size: with value extraction ("pick"), and streamed. */
#define HOST_ANSWER_MAX        (256 * 1024)
#define HOST_ANSWER_PIECES_MAX (4 * 1024 * 1024)
APP_EXPORT(app_on_data, void app_on_data(int32_t request, const uint8_t *data, int32_t length,
                                         int32_t flags, int32_t http_status));

/* The answer to a poll with "pick" in the manifest. "pick" is a list of
 * JSON paths, for example "current.temperature_2m" or
 * "daily.weather_code[0]". The host reads the answer and extracts the
 * values. The application receives this single call instead of pieces.
 *
 * request:     poll number in the manifest.
 * http_status: HTTP status, or a HOST_FAILED_* code.
 *
 * data_value() is valid inside the handler. It copies the value of path
 * number `index` as text without a terminator and returns its length.
 * Numbers, true, false and null are copied as written; strings are copied
 * without quotes. A value is at most HOST_VALUE_MAX bytes; a longer one
 * is cut between characters.
 * Returns HOST_ERR_NOT_FOUND if the answer does not contain the path,
 * HOST_ERR_NO_SPACE if the buffer is too small.
 * sdk/lib/app_values.h converts values to numbers and text. */
#define HOST_VALUE_MAX 63
APP_EXPORT(app_on_values, void app_on_values(int32_t request, int32_t http_status));
HOST_IMPORT(data_value, int32_t data_value(int32_t index, char *text, int32_t capacity));

/* Response metadata. Valid inside app_on_data() and app_on_values(), for
 * the answer being delivered. The same for every piece of one answer.
 * A header that the server did not send is an empty string. A header
 * longer than its field is cut. */
typedef struct host_response {
    int32_t status;            /* HTTP status, or a HOST_FAILED_* code */
    int32_t length;            /* Content-Length; -1 if absent */
    int32_t retry_after;       /* Retry-After in seconds; 0 if absent or given as a date */
    char    content_type[48];  /* "application/json; charset=utf-8" */
    char    etag[64];          /* for If-None-Match */
    char    last_modified[32]; /* for If-Modified-Since */
    char    location[128];     /* target of a redirect that was not followed */
    char    link[192];         /* Link header: address of the next page */
} host_response_t;
HOST_IMPORT(data_response, int32_t data_response(host_response_t *response));

/* Returns the buffer for one piece of data: HOST_DATA_CHUNK_MAX bytes of
 * application memory. Provided by sdk/lib; applications do not define it. */
APP_EXPORT(app_data_buffer, uint8_t *app_data_buffer(void));

/* ---- Localized strings ----
 *
 * Application text is displayed in the language of the device.
 * Translations are provided by the developer in the `strings` folder next
 * to the manifest: one file per language (en.json, ru.json), each an
 * object with the same keys:
 *
 *     { "no_events": "No events", "starts_in": "In {0} min" }
 *
 * tools/mkapp generates strings.h: the constants STR_NO_EVENTS,
 * STR_STARTS_IN and the function app_string(id). English is compiled into
 * the module. Every other language is written as a table file.
 * When the application is loaded, the host copies the table of the device
 * language over the English table. A language without a table is shown
 * in English.
 * The build fails if a language is not listed in sdk/languages.json, if a
 * translation has a missing or an extra key, or if a string contains a
 * character that the glyph packs of the language do not cover.
 *
 * Table format: 16 bits buffer size in bytes, 16 bits string count, one
 * 16-bit offset from the table start per string, then the strings,
 * null-terminated. */
APP_EXPORT(app_strings_buffer, uint8_t *app_strings_buffer(void));

/* Replaces "{0}", "{1}" ... in `pattern` with the arguments of those
 * numbers. Arguments can appear in any order.
 * An argument is a text; if `text` is NULL, it is a number.
 * Writes at most `capacity` bytes without a terminator, cut between
 * characters. Returns the length. */
typedef struct host_text_argument {
    int32_t     number;
    const char *text;
    int32_t     length;
} host_text_argument_t;
#define HOST_TEXT_ARGUMENTS_MAX 4
HOST_IMPORT(text_fill, int32_t text_fill(const char *pattern, int32_t pattern_length,
                                         const host_text_argument_t *arguments, int32_t count,
                                         char *text, int32_t capacity));

/* The user has saved the application settings on the device web page.
 * Settings are declared in the manifest. Each value is stored as text
 * under the key of the setting and is read with kv_get().
 * Called on and off the screen, like the data handlers. */
APP_EXPORT(app_on_settings, void app_on_settings(void));

/* Draws the complete frame from the application state. The surface is
 * white on entry. Drawing calls work only inside this handler.
 * Called after request_redraw() and whenever the host rebuilds the frame.
 * The host compares frames and decides whether and how to refresh the
 * panel. */
APP_EXPORT(app_on_draw, void app_on_draw(void));

/* ---- Drawing (inside app_on_draw() only) ---- */

/* Surface size: 400 x 300 for a full-screen application, 400 x 260 below
 * the host header otherwise. The origin is the top left corner. The
 * default colour is black. Drawing is clipped to the surface. */
HOST_IMPORT(gfx_width, int32_t gfx_width(void));
HOST_IMPORT(gfx_height, int32_t gfx_height(void));

/* Text.
 *
 * All text in this API is UTF-8. Every font size contains printable ASCII.
 * Any other character is drawn with the first available option:
 *   1. its glyph, if the user has installed the glyph pack (Latin with
 *      diacritics, Cyrillic, Greek);
 *   2. an ASCII transliteration ("Moskva", "Strasse");
 *   3. '?'.
 * The degree sign ("\xC2\xB0") is built into every size.
 * Pass text unchanged, measure it, and cut it with gfx_text_fit(), not by
 * bytes.
 *
 * A font constant selects a size. The typeface is a user setting:
 * Departure Mono (monospaced) or Jersey (proportional). Character widths
 * differ, so layout must use measured widths: gfx_text_width(),
 * gfx_font_height(), gfx_font_ascent(), gfx_font_cap_height().
 *
 * Sizes in pixels, Departure Mono / Jersey:
 *                       capitals   line       characters per 400 px
 *   HOST_FONT_SMALL      8 / 10    15 / 21    57 / 50
 *   HOST_FONT_TEXT      16 / 15    30 / 28    28 / 33
 *   HOST_FONT_MEDIUM    24 / 25    45 / 43    19 / 21
 *   HOST_FONT_LARGE     32 / 30    60 / 56    14 / 16
 *   HOST_FONT_DISPLAY   48 / 50    90 / 86     9 / 10
 *   HOST_FONT_HUGE      80 / 80   150 / 140    5 / 6   */
#define HOST_FONT_TEXT    0 /* body text; the host's own text */
#define HOST_FONT_MEDIUM  1
#define HOST_FONT_LARGE   2
#define HOST_FONT_DISPLAY 3
#define HOST_FONT_HUGE    4 /* five digits fill the width */
#define HOST_FONT_SMALL   5 /* units and captions */
/* Tabular digits: every digit has the width of the widest one. Use for
 * numbers that change in place, such as a clock or a countdown:
 *
 *     gfx_text(x, y, HOST_FONT_HUGE | HOST_FONT_TABULAR, "12:07", 5);
 *
 * Every call that takes a font accepts the flag. Pass the same flag when
 * measuring. Characters other than digits keep their own width. */
#define HOST_FONT_TABULAR 0x100

/* Draws text. `y` is the top of the line, not the baseline. Text longer
 * than HOST_TEXT_MAX bytes is cut. Returns the width drawn. */
#define HOST_TEXT_MAX 127
HOST_IMPORT(gfx_text, int32_t gfx_text(int32_t x, int32_t y, int32_t font, const char *text,
                                       int32_t length));
HOST_IMPORT(gfx_text_width, int32_t gfx_text_width(int32_t font, const char *text,
                                                   int32_t length));

/* Shortens `text` to `max_width` pixels. A text that does not fit is cut
 * between characters and "..." is appended. Writes the result to `fitted`
 * without a terminator. Returns its length; 0 if "..." alone does not fit. */
HOST_IMPORT(gfx_text_fit, int32_t gfx_text_fit(int32_t font, const char *text, int32_t length,
                                               int32_t max_width, char *fitted,
                                               int32_t capacity));

/* Font metrics, measured from the top of the line:
 *   gfx_font_height()      line height, including descenders;
 *   gfx_font_ascent()      distance to the baseline;
 *   gfx_font_cap_height()  height of capitals and digits above the baseline.
 * To place the top of the digits at `top`, draw the text at
 * top - (gfx_font_ascent() - gfx_font_cap_height()). */
HOST_IMPORT(gfx_font_height, int32_t gfx_font_height(int32_t font));
HOST_IMPORT(gfx_font_ascent, int32_t gfx_font_ascent(int32_t font));
HOST_IMPORT(gfx_font_cap_height, int32_t gfx_font_cap_height(int32_t font));

/* Sets the colour for the following calls: HOST_PAPER (0) to HOST_INK
 * (16). The colour is HOST_INK at the start of every app_on_draw().
 * Intermediate values are greys produced by dithering: N of every 16
 * pixels are inked, in a fixed 4 x 4 pattern. Use greys for filled areas
 * only; thin lines and small text become unreadable. */
#define HOST_PAPER 0
#define HOST_INK   16
HOST_IMPORT(gfx_color, int32_t gfx_color(int32_t color));

#define HOST_RECT_OUTLINE 0
#define HOST_RECT_FILLED  1
HOST_IMPORT(gfx_rect, int32_t gfx_rect(int32_t x, int32_t y, int32_t width, int32_t height,
                                       int32_t fill));
HOST_IMPORT(gfx_line, int32_t gfx_line(int32_t x0, int32_t y0, int32_t x1, int32_t y1));

/* Draws a bitmap. `bits`: rows from top to bottom, the most significant
 * bit is the left pixel, each row padded to a whole byte. Set bits are
 * drawn in the current colour; clear bits are transparent. */
HOST_IMPORT(gfx_bitmap, int32_t gfx_bitmap(int32_t x, int32_t y, int32_t width, int32_t height,
                                           const uint8_t *bits));

/* ---- Screen ---- */

/* Requests a redraw: the host calls app_on_draw().
 *
 * When the panel shows the frame depends on the calling handler:
 *   app_on_button(), app_on_back():  immediately;
 *   app_on_timer(), app_on_data(), app_on_alarm(), app_on_intent(),
 *   app_on_settings():               when the refresh policy of the user
 *                                    allows, by default once a minute.
 *
 * Flags:
 *   HOST_REDRAW_NORMAL    Default. Use after a button press and for data
 *                         that can wait.
 *   HOST_REDRAW_SOON      Show immediately without a button press. Use
 *                         for data the user is waiting for.
 *   HOST_REDRAW_CLEAN     Full refresh: 2 - 3 s, one flash, removes
 *                         ghosting. Use for an image that must be clean.
 *   HOST_REDRAW_NO_BLINK  One frame of a sequence: show as soon as the
 *                         panel is ready, never as the periodic full
 *                         refresh. Use for list navigation and for values
 *                         that change every second.
 *
 * SOON and CLEAN refreshes are at least one second apart.
 *
 * Frame sequences (NO_BLINK). A partial refresh takes about 0.5 s plus
 * the time of app_on_draw(); the maximum rate is about three frames in
 * two seconds. Timer events do not queue: a timer that expires while the
 * panel is busy fires once when the panel is ready. To keep time, select
 * the frame by time_uptime_ms() in app_on_draw(). Ghosting accumulates
 * during a sequence. End a sequence with HOST_REDRAW_CLEAN; otherwise
 * ghosting stays until the next full refresh or until the application
 * leaves the screen.
 *
 * The host does not limit redraw requests. Every refresh uses battery
 * energy and wears the panel. */
#define HOST_REDRAW_NORMAL   0
#define HOST_REDRAW_SOON     1
#define HOST_REDRAW_CLEAN    2
#define HOST_REDRAW_NO_BLINK 4
HOST_IMPORT(request_redraw, void request_redraw(int32_t flags));

/* Sets application text in the host header. The header is shown above an
 * application that is not full-screen. Host defaults: left side empty
 * (next to the connection icon), right side the date.
 * The text is drawn in the host font and size. A text that does not fit
 * is cut with "...". The two sides never overlap.
 * Length 0 returns the side to the host. A text of one space leaves the
 * side empty.
 * Not a drawing call: callable from any handler. The text is cleared when
 * the application leaves the screen. */
#define HOST_HEADER_LEFT     0
#define HOST_HEADER_RIGHT    1
#define HOST_HEADER_TEXT_MAX 63
HOST_IMPORT(header_text, int32_t header_text(int32_t side, const char *text, int32_t length));

/* Sets a status in the host header: an icon and a short text shown above
 * every screen with a header while the application is off the screen.
 * Use for an activity the user started, such as a countdown.
 * Layout: icon text ... text icon. On HOST_HEADER_LEFT the icon replaces
 * the host icon and the text follows it. On HOST_HEADER_RIGHT the text
 * replaces the date and the icon follows it.
 * A side belongs to the application that set it last. Length 0 with
 * HOST_ICON_NONE releases the side, if this application still owns it.
 * The side is also released when the application is removed and when the
 * device restarts.
 * Callable from any handler, on and off the screen. A change is shown as
 * a data-driven redraw. */
#define HOST_STATUS_TEXT_MAX 31
HOST_IMPORT(header_status, int32_t header_status(int32_t side, int32_t icon, const char *text,
                                                 int32_t length));

/* Draws a QR code of `text`. (x, y) is the top left corner; `cell` is the
 * module size in pixels. The quiet zone of four modules is not drawn; the
 * application leaves it.
 * Returns the number of modules per side. With cell 0 nothing is drawn
 * and the call is valid in any handler.
 * Returns HOST_ERR_NO_SPACE if the text does not fit the largest code
 * (57 modules per side). */
#define HOST_QR_TEXT_MAX 200
HOST_IMPORT(gfx_qr, int32_t gfx_qr(int32_t x, int32_t y, int32_t cell, const char *text,
                                   int32_t length));

/* Draws a bitmap that the host reads from an application file, starting
 * at `offset`. The format is that of gfx_bitmap(). A file can hold
 * several bitmaps. The bitmap does not use application memory.
 * Returns HOST_ERR_NOT_FOUND if the file does not exist or ends before
 * the bitmap does. */
HOST_IMPORT(gfx_bitmap_file,
            int32_t gfx_bitmap_file(int32_t x, int32_t y, int32_t width, int32_t height,
                                    const char *name, int32_t name_length, int32_t offset));

/* Draws a built-in icon in the current colour. An icon is 11 x 11 cells;
 * (x, y) is its top left corner; `cell` is the cell size in pixels, 1 to
 * HOST_ICON_CELL_MAX. Cell size 2 matches the host text size.
 * Returns HOST_ERR_ARGUMENT for an unknown icon. */
#define HOST_ICON_SIGNAL     0
#define HOST_ICON_NO_SIGNAL  1
#define HOST_ICON_UP         2
#define HOST_ICON_DOWN       3
#define HOST_ICON_LEFT       4
#define HOST_ICON_RIGHT      5
#define HOST_ICON_CHECK      6
#define HOST_ICON_CROSS      7
#define HOST_ICON_PLUS       8
#define HOST_ICON_MINUS      9
#define HOST_ICON_TRASH      10
#define HOST_ICON_GEAR       11
#define HOST_ICON_CLOCK      12
#define HOST_ICON_HOURGLASS  13
#define HOST_ICON_BELL       14
#define HOST_ICON_INFO       15
#define HOST_ICON_WARNING    16
#define HOST_ICON_PLAY       17
#define HOST_ICON_PAUSE      18
#define HOST_ICON_UPDATE     19
#define HOST_ICON_CELLS      11
#define HOST_ICON_CELL_MAX   16
HOST_IMPORT(gfx_icon, int32_t gfx_icon(int32_t icon, int32_t x, int32_t y, int32_t cell));

/* Draws an application picture, as generated by tools/mkpicture from an
 * image or from text rows of '#' and '.'.
 * Format: one byte width in cells, one byte height in cells, then one bit
 * per cell; 1 is drawn in the current colour, the leftmost cell is the
 * high bit, each row is padded to a whole byte.
 * `cell` is the cell size in pixels, 1 to HOST_ICON_CELL_MAX.
 * tools/mkapp generates pictures.h from the `pictures` folder of an
 * application. */
HOST_IMPORT(gfx_picture,
            int32_t gfx_picture(int32_t x, int32_t y, const uint8_t *picture, int32_t cell));

/* ---- Kit: host interface parts ----
 *
 * The ui_* calls draw the parts that the host screens are built from. The
 * drawing code is in the host and is the same code that draws the menu
 * and the host questions. Valid inside app_on_draw() only. */

/* List.
 *
 * One row per line. The selected row is inverted. A scroll bar is drawn
 * when the rows do not fit. The selected row is kept in the middle.
 * The application keeps the list state and calls ui_list_row() for the
 * visible rows:
 *
 *     static host_list_t list = { .count = 12 };
 *
 *     int32_t first = ui_list_first(&list);
 *     for (int32_t i = first; i < first + ui_list_visible(&list) && i < list.count; i++) {
 *         ui_list_row(&list, i, label, label_length, value, value_length, HOST_ICON_NONE);
 *     }
 *
 * A list takes the full width of the surface, and `height` pixels from
 * `top` (0: to the bottom). ui_title() returns a suitable `top`. For a
 * ui_actions() row below the list, reduce the height by
 * HOST_UI_ACTIONS_HEIGHT.
 *
 * selected = -1: a list without a selection. UP and DOWN scroll it;
 * `first` is the top row. sdk/lib/app_ui.h handles the buttons for both
 * kinds.
 *
 * Row layout: `value` is right-aligned; length 0 omits it. Label and
 * value never overlap. If both do not fit, a value up to half the row
 * width is kept whole and the label is cut with "..."; a longer value
 * gets the space the label leaves, at least half the row.
 * `icon` is a HOST_ICON_* drawn at the right edge, or HOST_ICON_NONE.
 * HOST_ICON_CHECK marks the active option. Rows outside the visible range
 * are not drawn. */
#define HOST_ICON_NONE (-1)
typedef struct host_list {
    int32_t count;
    int32_t selected; /* or -1 */
    int32_t top;
    int32_t height;   /* 0: to the bottom of the surface */
    int32_t first;    /* without a selection: the top row */
} host_list_t;
HOST_IMPORT(ui_list_visible, int32_t ui_list_visible(const host_list_t *list));
HOST_IMPORT(ui_list_first, int32_t ui_list_first(const host_list_t *list));
HOST_IMPORT(ui_list_row,
            int32_t ui_list_row(const host_list_t *list, int32_t index, const char *label,
                                int32_t label_length, const char *value, int32_t value_length,
                                int32_t icon));

/* Notice: a full-surface message in the style of the host.
 *
 * Layout: two lines of large text on the left, a large icon on the right,
 * button labels at the bottom ("<ok> OK" at the left, "BACK <back>" at the
 * right). With `is_working`, three strokes replace the button labels.
 * All texts are null-terminated. Empty texts are not drawn. Without
 * button labels the notice takes the full height.
 * A destructive question must ignore presses made before it was shown on
 * the panel. */
typedef struct host_notice {
    char    first[64];  /* UTF-8: a field holds half as many two-byte characters */
    char    second[64];
    char    detail[96]; /* one line of body text below the large lines */
    char    ok[32];     /* OK action: "Remove" */
    char    back[32];   /* BACK action: "Cancel" */
    int32_t icon;       /* HOST_ICON_*, or HOST_ICON_NONE */
    int32_t is_working;
} host_notice_t;
HOST_IMPORT(ui_notice, int32_t ui_notice(const host_notice_t *notice));

/* Queues a notification. The host shows the notice below its header,
 * over the current screen, until the user presses OK or BACK, and then
 * restores the previous screen.
 * Callable from any handler, typically app_on_alarm() off the screen.
 * The application is not loaded while the notification is shown and is
 * not told when it is dismissed.
 * Fields used: first (required), second, detail, icon.
 * Several notifications queue. A restart clears the queue.
 * Returns HOST_OK, HOST_ERR_ARGUMENT, or HOST_ERR_NO_SPACE if the queue
 * is full. */
HOST_IMPORT(notify, int32_t notify(const host_notice_t *notice));

/* Text in a host style. `y` is the top of the line. The text is cut with
 * "..." at `max_width` pixels (0: no limit). Returns the width drawn.
 * Using the kit is optional; gfx_* calls can draw anything.
 * sdk/lib/app_ui.h implements button handling for the list, the picker
 * and the date editor. */
#define HOST_UI_TEXT    0 /* body text */
#define HOST_UI_TITLE   1 /* page title */
#define HOST_UI_CAPTION 2 /* caption next to a larger element */
#define HOST_UI_VALUE   3 /* the main value of a screen */
HOST_IMPORT(ui_text, int32_t ui_text(int32_t x, int32_t y, int32_t style, const char *text,
                                     int32_t length, int32_t max_width));

/* ui_text_lines: text wrapped at spaces to `width`, at most `max_lines`
 * lines (1..HOST_UI_LINES_MAX). The last line is cut with "..." if text
 * remains. Line height is ui_text_height() of the style. Returns the
 * number of lines drawn.
 *
 * ui_text_width, ui_text_height: text width and line height of a style.
 * Styles map to fonts: HOST_UI_TEXT = HOST_FONT_TEXT, HOST_UI_TITLE =
 * HOST_FONT_MEDIUM, HOST_UI_CAPTION = HOST_FONT_SMALL, HOST_UI_VALUE =
 * HOST_FONT_DISPLAY. The gfx_font_*() metrics of the font apply. */
#define HOST_UI_LINES_MAX 8
HOST_IMPORT(ui_text_lines, int32_t ui_text_lines(int32_t x, int32_t y, int32_t style,
                                                 const char *text, int32_t length, int32_t width,
                                                 int32_t max_lines));
HOST_IMPORT(ui_text_width, int32_t ui_text_width(int32_t style, const char *text,
                                                 int32_t length));
HOST_IMPORT(ui_text_height, int32_t ui_text_height(int32_t style));

/* Standard left and right margin of the host screens. */
#define HOST_UI_MARGIN 16

/* Page title at the standard position, with a rule below it. Returns the
 * y of the content area. */
HOST_IMPORT(ui_title, int32_t ui_title(const char *text, int32_t length));

/* Built-in icon at a standard size. */
#define HOST_UI_ICON_SMALL  0 /* 22 px: next to text */
#define HOST_UI_ICON_MEDIUM 1 /* 44 px */
#define HOST_UI_ICON_LARGE  2 /* 99 px: notice icon */
HOST_IMPORT(ui_icon, int32_t ui_icon(int32_t x, int32_t y, int32_t icon, int32_t size));

/* One button label: the button name ("UP") white on black, and the action
 * text after it, or before it if `mirrored`. `x` is the left edge of the
 * whole label; `y` is the top of the text line. Returns the width. The
 * width cannot be measured without drawing; use ui_actions() for
 * right-aligned labels. */
HOST_IMPORT(ui_button, int32_t ui_button(int32_t x, int32_t y, const char *name,
                                         int32_t name_length, const char *action,
                                         int32_t action_length, int32_t mirrored));

/* Button row at the bottom of the surface, HOST_UI_ACTIONS_HEIGHT pixels
 * high: the OK action at the left, the BACK action at the right. Length 0
 * omits a button. A single button is centred. */
#define HOST_UI_ACTIONS_HEIGHT 44
HOST_IMPORT(ui_actions, int32_t ui_actions(const char *ok, int32_t ok_length, const char *back,
                                           int32_t back_length));

/* Bar: a frame filled from the left in proportion `value` / `maximum`. */
HOST_IMPORT(ui_bar, int32_t ui_bar(int32_t x, int32_t y, int32_t width, int32_t value,
                                   int32_t maximum));

/* A value followed by its unit on the same baseline: "18 km/h",
 * "763 ppm", "25 min". The unit size is derived from the value size.
 * `y` is the top of the value line. `font` is a HOST_FONT_*, optionally
 * with HOST_FONT_TABULAR. Unit length 0 omits the unit.
 * Both calls return the total width. */
HOST_IMPORT(ui_value, int32_t ui_value(int32_t x, int32_t y, int32_t font, const char *value,
                                       int32_t value_length, const char *unit,
                                       int32_t unit_length));
HOST_IMPORT(ui_value_width, int32_t ui_value_width(int32_t font, const char *value,
                                                   int32_t value_length, const char *unit,
                                                   int32_t unit_length));

/* Draws the standard notice for a request status on the whole surface.
 *
 * status:  0 if no request was made yet, a HOST_FAILED_* code, or an HTTP
 *          status.
 * Notices: 0 "Loading"; HOST_FAILED_* one per code ("Not set up",
 *          "No network", "No answer" ...); 401, 403 "Access denied";
 *          404 "Not found"; any other non-2xx status "No answer" with
 *          the code.
 * Returns 1 if a notice was drawn. Returns 0 and draws nothing for a 2xx
 * status; the application reports missing data itself. */
HOST_IMPORT(ui_reason, int32_t ui_reason(int32_t status));

/* Frame around the value that UP and DOWN change, with an arrow above
 * and below for each available direction. */
#define HOST_UI_ARROW_UP   1
#define HOST_UI_ARROW_DOWN 2
HOST_IMPORT(ui_frame, int32_t ui_frame(int32_t x, int32_t y, int32_t width, int32_t height,
                                       int32_t arrows));

/* Cell: a name at the top, and below it a picture and a value side by
 * side, in a box of any size. The value is drawn at the largest size
 * that fits.
 * Picture size: HOST_UI_CELL_PICTURE_LARGE pixels in a cell that covers
 * the whole surface, HOST_UI_CELL_PICTURE_SMALL otherwise.
 * `picture` is a picture in gfx_picture() format, or 0.
 * Cells shown together should use one size: ui_cell_fit() returns the
 * size class that fits a cell (1 is the largest); set `size` of every
 * cell to the largest returned number. size 0 selects the size
 * automatically. */
#define HOST_UI_CELL_PICTURE_LARGE 88
#define HOST_UI_CELL_PICTURE_SMALL 44
typedef struct host_cell {
    int32_t x, y, width, height;
    char    name[48];  /* empty: no name */
    char    value[48];
    int32_t size;
} host_cell_t;
HOST_IMPORT(ui_cell_fit, int32_t ui_cell_fit(const host_cell_t *cell, const uint8_t *picture));
HOST_IMPORT(ui_cell, int32_t ui_cell(const host_cell_t *cell, const uint8_t *picture));

/* Chart of a series of integers in a box. The vertical scale spans the
 * least and the greatest value of the series.
 * `values`: `count` numbers, 1 to HOST_CHART_VALUES_MAX, oldest first.
 * A value of HOST_CHART_NO_VALUE is a gap in the series.
 * kind: HOST_CHART_LINE - points joined by a line; HOST_CHART_AREA - the
 * same with the area under the line in grey; HOST_CHART_BARS - one bar
 * per value; HOST_CHART_STEPS - a value holds until the next one.
 * flags: HOST_CHART_AXIS - a rule at the baseline; HOST_CHART_FROM_ZERO -
 * the scale includes 0; HOST_CHART_RANGE - the greatest and the least
 * value as captions at the left, printed with `decimals` decimals (215
 * with 1 is "21.5"); HOST_CHART_MARK_LAST - a mark at the last value
 * (LINE, AREA, STEPS).
 * Returns HOST_OK or HOST_ERR_ARGUMENT. */
#define HOST_CHART_LINE  0
#define HOST_CHART_AREA  1
#define HOST_CHART_BARS  2
#define HOST_CHART_STEPS 3
#define HOST_CHART_AXIS      1
#define HOST_CHART_FROM_ZERO 2
#define HOST_CHART_RANGE     4
#define HOST_CHART_MARK_LAST 8
#define HOST_CHART_VALUES_MAX 128
#define HOST_CHART_NO_VALUE   INT32_MIN
typedef struct host_chart {
    int32_t x, y, width, height;
    int32_t kind;
    int32_t flags;
    int32_t decimals;
    int32_t count;
} host_chart_t;
HOST_IMPORT(ui_chart, int32_t ui_chart(const host_chart_t *chart, const int32_t *values));

/* Picker: one value of a sequence, on the whole surface. The selected
 * value is framed in the middle; the previous value is above it, the next
 * one below. An empty text marks the end of the sequence. The button row
 * is included. */
typedef struct host_picker {
    char above[64];
    char chosen[64];
    char below[64];
    char ok[32];
    char back[32];
} host_picker_t;
HOST_IMPORT(ui_picker, int32_t ui_picker(const host_picker_t *picker));

/* Date and time editor on the whole surface. The part being set is
 * framed. */
#define HOST_DATE_HAS_TIME 1
#define HOST_DATE_HAS_DATE 2
#define HOST_DATE_HOUR     0
#define HOST_DATE_MINUTE   1
#define HOST_DATE_DAY      2
#define HOST_DATE_MONTH    3
#define HOST_DATE_YEAR     4
#define HOST_DATE_NONE     5 /* display only: no part is being set */
typedef struct host_date {
    int32_t year, month, day; /* month 1..12 */
    int32_t hour, minute;
    int32_t has;              /* HOST_DATE_HAS_* */
    int32_t selected;         /* HOST_DATE_HOUR .. HOST_DATE_NONE */
    char    ok[32];
    char    back[32];
} host_date_t;
HOST_IMPORT(ui_date, int32_t ui_date(const host_date_t *date));

/* ---- Navigation ----
 *
 * Navigation is a stack with the menu at the bottom. A host question or
 * an application that took the screen is placed on top; when it closes,
 * the entry below it returns. A long press of BACK clears the stack and
 * opens the menu.
 *   - An application covered by another entry is unloaded and loaded
 *     again when it returns: app_on_blur() and app_on_terminate() before,
 *     app_init() and app_on_focus() after. Keep the state to restore in
 *     the store.
 *   - request_exit() goes one step down the stack: to the entry the
 *     application was opened over, or to the menu.
 *   - An application that took the screen with request_focus() is placed
 *     over the current screen and returns the user to it on exit. */

/* Requests the screen from a background handler (app_on_data(),
 * app_on_alarm() off the screen). The host decides whether to grant it.
 * Ignored on the screen. */
HOST_IMPORT(request_focus, void request_focus(void));

/* Closes the application: one step down the stack. Takes effect when
 * app_on_button(), app_on_back() or app_on_timer() returns; ignored in
 * other handlers. A long press of BACK closes any application without
 * calling it. */
HOST_IMPORT(request_exit, void request_exit(void));

/* One-shot timer: app_on_timer(id) after `milliseconds`. 0 cancels.
 * Setting a running timer restarts it. id: 0..HOST_TIMER_COUNT-1.
 * Timers stop when the application leaves the screen. */
#define HOST_TIMER_COUNT 4
HOST_IMPORT(timer_set, int32_t timer_set(int32_t id, int32_t milliseconds));

/* ---- Store ---- */

/* Key-value store for small persistent values. Each application has a
 * private store. Writing wears the flash memory: write when data has
 * changed, for example in app_on_terminate(), not on every timer tick. */
#define HOST_KV_KEY_MAX   15  /* 1..15 characters from '!' to '~' */
#define HOST_KV_VALUE_MAX 1024
#define HOST_KV_TOTAL_MAX 4096 /* all values of one application */

/* Reads a value into `value`. Returns its length. */
HOST_IMPORT(kv_get, int32_t kv_get(const char *key, int32_t key_length, void *value,
                                   int32_t capacity));

/* Writes a value. Length 0 deletes the key. */
HOST_IMPORT(kv_set, int32_t kv_set(const char *key, int32_t key_length, const void *value,
                                   int32_t length));

/* ---- Network ---- */

/* Network availability is reported in the answer: a request made without
 * a network fails with HOST_FAILED_NO_NETWORK. The radio can be off
 * between requests. */

/* Runs poll number `poll` of the manifest immediately. The answer is
 * delivered as usual. The host performs one request at a time, at most
 * one per second; a repeated call before the answer has no effect. */
HOST_IMPORT(poll_now, int32_t poll_now(int32_t poll));

/* Queues a request from code.
 *
 * Destination: only hosts listed under "hosts" in the manifest.
 * Queue:       the host performs one request at a time for the whole
 *              device and keeps at most HOST_HTTP_PENDING_MAX waiting,
 *              for all applications together.
 * Answer:      delivered through app_on_data() in pieces, with `tag` as
 *              the request number, on or off the screen. The instance
 *              that receives the answer is not the one that made the
 *              request.
 * tag:         a number chosen by the application, HOST_REQUEST_FIRST to
 *              HOST_REQUEST_LAST, distinct from poll numbers:
 *
 *                  enum { REQUEST_TOKEN = HOST_REQUEST_FIRST, REQUEST_EVENTS };
 *
 * headers:     lines "Name: value" separated by '\n', at most four.
 * {{key}}      in the URL and in header values is replaced by the setting
 *              with that key, as in a poll.
 *
 * Redirects: a GET is followed up to three times; the application
 * receives the last answer only. A redirect to a host outside "hosts"
 * fails with HOST_FAILED_NOT_ALLOWED. Request headers are not sent to
 * another host. A request with any other method is sent once; its 3xx
 * status is delivered as the answer. Polls are followed the same way,
 * to any host.
 *
 * A JSON answer can be parsed with sdk/lib/json_pick.h: feed every piece,
 * read the values after the last one.
 *
 * Returns HOST_OK; HOST_ERR_ARGUMENT for a URL outside the manifest hosts
 * or malformed arguments; HOST_ERR_NO_SPACE if the queue is full. */
#define HOST_HTTP_GET    0
#define HOST_HTTP_POST   1
#define HOST_HTTP_PUT    2
#define HOST_HTTP_DELETE 3
#define HOST_HTTP_PATCH    4
#define HOST_HTTP_HEAD     5 /* the answer is one piece without data; use data_response() */
#define HOST_HTTP_PROPFIND 6 /* CalDAV */
#define HOST_HTTP_REPORT   7 /* CalDAV */
#define HOST_HTTP_METHOD_LAST HOST_HTTP_REPORT
#define HOST_HTTP_HEADERS_MAX 1023
#define HOST_HTTP_BODY_MAX    512
#define HOST_HTTP_PENDING_MAX 2
#define HOST_REQUEST_FIRST    16
#define HOST_REQUEST_LAST     255
HOST_IMPORT(http_request,
            int32_t http_request(int32_t tag, int32_t method, const char *url, int32_t url_length,
                                 const char *headers, int32_t headers_length,
                                 const void *body, int32_t body_length));

/* Sets the interval of poll number `poll`, overriding the manifest:
 * 10 seconds to 1 day, counted from now. The interval persists across
 * restarts until it is set again or the application is removed together
 * with its store. */
HOST_IMPORT(poll_set_interval, int32_t poll_set_interval(int32_t poll, int32_t seconds));

/* ---- Intents ----
 *
 * Device events delivered to an application on or off the screen. The
 * manifest lists the events to receive:
 *
 *     "intents": ["time_changed", "network_up", "started"]
 *
 * Each event is delivered through app_on_intent(). An application that is
 * not loaded is loaded for the call. An application without "intents"
 * receives none.
 *
 * app_init() is called before the handler, as before any handler. The
 * instance is the one on the screen if the application is there;
 * otherwise it is loaded for the call and unloaded after it.
 *
 * HOST_INTENT_STARTED is delivered after the device has started and shown
 * its first screen. An application reopened after the restart can be
 * drawn before the intent arrives; app_init() must not depend on it.
 * HOST_INTENT_TIME_CHANGED is also delivered when the device obtains the
 * time for the first time. time_now() already returns the new time inside
 * the handler. Corrections of a few seconds are not reported. */
#define HOST_INTENT_TIME_CHANGED 1 /* the clock or the time zone was changed: recompute
                                      values derived from the time */
#define HOST_INTENT_NETWORK_UP   2 /* the device is connected to its network again */
#define HOST_INTENT_STARTED      3 /* the device was powered up or restarted: restore from
                                      the store what a restart clears - alarms, poll
                                      intervals set from code, pending requests */
APP_EXPORT(app_on_intent, void app_on_intent(int32_t intent));

/* ---- Alarms ---- */

/* An alarm calls app_on_alarm(id) once at its time, on or off the screen.
 * Off the screen the application is loaded for the call and can request
 * the screen with request_focus(). An application has HOST_ALARM_COUNT
 * alarms, numbered from 0. Setting an alarm again moves it.
 * timer_set() works only on the screen; alarms work everywhere.
 *
 * alarm_set():    relative, `seconds` from now, at least 1. Independent
 *                 of clock changes. 0 cancels the alarm.
 * alarm_set_at(): absolute, at `unix_time` as counted by time_now().
 *                 Follows clock changes.
 * Both return HOST_OK; HOST_ERR_ARGUMENT for an invalid number or time;
 * HOST_ERR_NO_SPACE if the device cannot hold another alarm.
 *
 * A device restart clears all alarms. To survive a restart, keep the time
 * in the store and set the alarm again in the "started" intent. A new
 * version of an application does not inherit the alarms of the old one. */
#define HOST_ALARM_COUNT 4
HOST_IMPORT(alarm_set, int32_t alarm_set(int32_t id, int32_t seconds));
HOST_IMPORT(alarm_set_at, int32_t alarm_set_at(int32_t id, int64_t unix_time));

/* Repeating alarm: app_on_alarm(id) every `seconds`, until alarm_set(id, 0)
 * cancels it or the alarm is set another way. The first call comes
 * `first_in` seconds from now; 0 means one full period. To align with the
 * minute, pass the seconds left in the current minute.
 * Period: HOST_ALARM_EVERY_MIN to HOST_ALARM_EVERY_MAX seconds. Every
 * call loads the application.
 * Typical use: keeping a header_status() text current while the
 * application is off the screen. */
#define HOST_ALARM_EVERY_MIN 60
#define HOST_ALARM_EVERY_MAX 86400
HOST_IMPORT(alarm_set_every, int32_t alarm_set_every(int32_t id, int32_t seconds,
                                                     int32_t first_in));
APP_EXPORT(app_on_alarm, void app_on_alarm(int32_t id));

/* ---- Files ---- */

/* Files hold data that does not fit in a store value: a picture, a table,
 * a cache. An application can read the files of its package (the `files`
 * folder at build time) and the files it has written.
 * Limit: HOST_FILES_MAX bytes for all files of one application, counted
 * as stored on the device: every file takes whole 4 KB blocks, 16 blocks
 * in total. The limit is guaranteed for every installed application.
 * Writing wears the flash memory, like kv_set().
 * File name: 1-23 characters of a-z, 0-9, '.', '-' and '_'; the first
 * character is not a dot. */
#define HOST_FILE_NAME_MAX 23
#define HOST_FILES_MAX     (64 * 1024)

/* Returns the file size, or HOST_ERR_NOT_FOUND. */
HOST_IMPORT(file_size, int32_t file_size(const char *name, int32_t name_length));

/* Reads up to `capacity` bytes starting at `offset`. Returns the number
 * of bytes read: fewer at the end of the file, 0 past it. */
HOST_IMPORT(file_read, int32_t file_read(const char *name, int32_t name_length, int32_t offset,
                                         void *bytes, int32_t capacity));

/* Writes `length` bytes at `offset`. The offset must be inside the file
 * or at its end: a file is extended piece by piece, and existing data can
 * be overwritten. Length 0 at offset 0 deletes the file.
 * Returns HOST_ERR_NO_SPACE if the files of the application would exceed
 * HOST_FILES_MAX. */
HOST_IMPORT(file_write, int32_t file_write(const char *name, int32_t name_length, int32_t offset,
                                           const void *bytes, int32_t length));

/* ---- Time ---- */

/* Returns Unix time in seconds; 0 while the device does not know the time.
 *
 * To measure time (a countdown, a stopwatch), keep the start time in the
 * store and compute the rest on demand. The application is not loaded
 * most of the time and cannot count ticks.
 * The clock runs through device restarts and sleep, but not through a
 * power loss. It is 0 until the network or the user sets it for the first
 * time. Until then use time_uptime_ms(), which also runs through sleep
 * and starts from 0 at every restart. */
HOST_IMPORT(time_now, int64_t time_now(void));

/* Returns the offset of local time from UTC in seconds, daylight saving
 * included; 0 while the time is unknown. */
HOST_IMPORT(time_zone_offset, int32_t time_zone_offset(void));

/* Fills the current local date and time. Returns HOST_ERR_NOT_FOUND while
 * the time is unknown. */
typedef struct {
    int32_t year;    /* 2026 */
    int32_t month;   /* 1..12 */
    int32_t day;     /* 1..31 */
    int32_t weekday; /* 0 is Sunday */
    int32_t hour;    /* 0..23 */
    int32_t minute;
    int32_t second;
} host_local_time_t;
HOST_IMPORT(time_local, int32_t time_local(host_local_time_t *now));

/* Converts a moment (Unix seconds) to local date and time, using the zone
 * offset in effect at that moment, daylight saving included.
 * Events with their own time zone must be converted before they reach the
 * application. */
HOST_IMPORT(time_local_at, int32_t time_local_at(int64_t unix_time, host_local_time_t *parts));

/* Formats a moment as text in the device language and local time. Writes
 * up to `capacity` bytes without a terminator. Returns the length.
 *   HOST_TIME_CLOCK      "15:04"
 *   HOST_TIME_DATE       "Fri 13 Nov"
 *   HOST_TIME_WEEKDAY    "Friday"
 *   HOST_TIME_LONG_DATE  "13 November"
 *   HOST_TIME_NEAR       relative: "15:04" today, "Tmrw 15:04",
 *                        "Sat 15:04" within a week, "21 Nov" beyond
 *   HOST_TIME_NEAR_DAY   relative, date only: "Today", "Tmrw", "Sat", "21 Nov" */
#define HOST_TIME_CLOCK     0
#define HOST_TIME_DATE      1
#define HOST_TIME_WEEKDAY   2
#define HOST_TIME_LONG_DATE 3
#define HOST_TIME_NEAR      4
#define HOST_TIME_NEAR_DAY  5
HOST_IMPORT(time_text, int32_t time_text(int64_t unix_time, int32_t style, char *text,
                                         int32_t capacity));

/* Converts a local date and time to Unix time, for example for
 * alarm_set_at(). The weekday field is ignored. Out-of-range values carry
 * over: day 32 of January is 1 February. */
HOST_IMPORT(time_from_local, int64_t time_from_local(const host_local_time_t *parts));

/* Returns milliseconds since the device started. Always available. */
HOST_IMPORT(time_uptime_ms, int64_t time_uptime_ms(void));

/* Returns an identifier of the current boot: different after every
 * restart, never 0. A stored time_uptime_ms() value is valid only within
 * the boot it was taken in. Store this identifier with it and discard the
 * value when the identifier differs. */
HOST_IMPORT(time_boot_id, uint32_t time_boot_id(void));

/* ---- Log ---- */

#define HOST_LOG_ERROR 1
#define HOST_LOG_WARN  2
#define HOST_LOG_INFO  3
#define HOST_LOG_DEBUG 4
/* Writes one line to the device log. A longer text is cut. Characters
 * other than printable ASCII, including line breaks, are logged as "?". */
#define HOST_LOG_TEXT_MAX 200
HOST_IMPORT(log, void host_log(int32_t level, const char *text, int32_t length));

/* ---- System API ----
 *
 * The sys_* calls are used by the host's own applications (Settings,
 * About). Any application can use them if its manifest contains
 * "system": true. The device web page warns the user before installing
 * such an application. A module that imports a sys_* call without this
 * declaration is rejected. */
#ifdef __wasm__
#define HOST_SYSTEM_IMPORT(name, ...) \
    __attribute__((import_module("host"), import_name(#name))) __VA_ARGS__
#else
#define HOST_SYSTEM_IMPORT(name, ...)
#endif

/* Device information as text. Returns the length; HOST_ERR_NO_SPACE if
 * the buffer is too small; 0 if the value is currently unknown (for
 * example, no address while the device is off the network). */
#define SYS_INFO_VERSION 0 /* firmware version of the build */
#define SYS_INFO_NAME    1 /* "Perinkpheral-C7B0" */
#define SYS_INFO_NETWORK 2 /* configured Wi-Fi network */
#define SYS_INFO_ADDRESS 3 /* address of the web page: on the network, or on the setup
                              access point */
#define SYS_INFO_WIFI    4 /* one of SYS_WIFI_* */
#define SYS_INFO_SLOT    5 /* running firmware slot: "ota_0" */
#define SYS_INFO_FREE_MEMORY 6 /* free host memory in KB, as digits */
#define SYS_INFO_TIME_ZONE   7 /* POSIX TZ string: "MSK-3" */
#define SYS_INFO_JOIN_CODE   8 /* QR code text that joins a phone to the setup access
                                  point: "WIFI:T:nopass;S:Perinkpheral-C7B0;;" */
#define SYS_WIFI_ONLINE     "online"
#define SYS_WIFI_CONNECTING "connecting"
#define SYS_WIFI_SETUP      "setup" /* waiting for network settings on the setup access point */
HOST_SYSTEM_IMPORT(sys_info, int32_t sys_info(int32_t what, char *text, int32_t capacity));

/* Draws the wordmark "perINKpheraL" in the square font. (x, y) is the top
 * left corner. `scale`: 1 to SYS_LOGO_SCALE_MAX; scale 5 is 357 px wide.
 * sys_logo() is a drawing call. The width and height calls are valid in
 * any handler. */
#define SYS_LOGO_SCALE_MAX 5
HOST_SYSTEM_IMPORT(sys_logo, int32_t sys_logo(int32_t x, int32_t y, int32_t scale));
HOST_SYSTEM_IMPORT(sys_logo_width, int32_t sys_logo_width(int32_t scale));
HOST_SYSTEM_IMPORT(sys_logo_height, int32_t sys_logo_height(int32_t scale));

/* Numeric host settings. sys_number_get() returns the value and its range. */
#define SYS_NUMBER_TIME_AUTOMATIC      0 /* 1: time from the network; 0: set manually */
#define SYS_NUMBER_REFRESH_FULL_EVERY  1 /* partial refreshes before a full one; 0: always full.
                                            SYS_REFRESH_FULL_NEVER is also accepted */
#define SYS_NUMBER_REFRESH_MIN_SECONDS 2 /* minimum interval between refreshes without a press */
#define SYS_NUMBER_REFRESH_FORCE_HOURS 3 /* maximum time without a refresh */
#define SYS_NUMBER_POWER_SAVING        4 /* 0: off; 1: on - the device sleeps when idle, the
                                            radio is on for polls and for a period after a
                                            press; 2: on unless a computer is connected to
                                            the USB port */
#define SYS_NUMBER_REFRESH_FAST        5 /* 1: fast full refresh */
#define SYS_NUMBER_FONT                6 /* typeface: 0 Departure Mono, 1 Jersey */
#define SYS_NUMBER_MENU_IDLE_MINUTES    7 /* idle time in the menu before the default
                                            application opens */
#define SYS_NUMBER_HEADER_CLOCK         8 /* 1: the header shows the time instead of the date */
#define SYS_REFRESH_FULL_NEVER         255
typedef struct {
    int32_t value;
    int32_t least;
    int32_t most;
} sys_number_t;
HOST_SYSTEM_IMPORT(sys_number_get, int32_t sys_number_get(int32_t what, sys_number_t *number));
/* Stores the value and applies it immediately. Returns HOST_ERR_ARGUMENT
 * for a value outside the range. */
HOST_SYSTEM_IMPORT(sys_number_set, int32_t sys_number_set(int32_t what, int32_t value));

/* Text host settings, at most SYS_TEXT_MAX bytes. */
#define SYS_TEXT_MAX 63
#define SYS_TEXT_TIME_ZONE 0 /* POSIX TZ string; read with sys_info() */
HOST_SYSTEM_IMPORT(sys_text_set, int32_t sys_text_set(int32_t what, const char *text, int32_t length));

/* Sets the clock, in Unix seconds. Returns HOST_ERR_FAILED while the time
 * source is the network. */
HOST_SYSTEM_IMPORT(sys_time_set, int32_t sys_time_set(int64_t unix_seconds));

/* Installed applications.
 * An application id is derived from its code and its manifest; another
 * version has another id.
 * sys_app_list():   writes up to `capacity` ids in menu order. Returns
 *                   the number of installed applications.
 * sys_app_title():  writes the title. Returns its length.
 * sys_app_remove(): removes the files of the application and keeps its
 *                   store.
 * All return HOST_ERR_NOT_FOUND for an id that is not installed. */
#define SYS_APP_LIST_MAX 16 /* maximum `capacity` */
HOST_SYSTEM_IMPORT(sys_app_list, int32_t sys_app_list(uint32_t *ids, int32_t capacity));
HOST_SYSTEM_IMPORT(sys_app_title, int32_t sys_app_title(uint32_t id, char *text, int32_t capacity));
HOST_SYSTEM_IMPORT(sys_app_remove, int32_t sys_app_remove(uint32_t id));

/* Default application: opened after a restart, after the menu idle time,
 * and by BACK in the menu.
 * sys_app_default() returns its id, or 0 if none is set.
 * sys_app_set_default(0) clears the setting. The setting is stored by
 * application name and survives an update of the application. */
HOST_SYSTEM_IMPORT(sys_app_default, uint32_t sys_app_default(void));
HOST_SYSTEM_IMPORT(sys_app_set_default, int32_t sys_app_set_default(uint32_t id));

/* Irreversible actions. The device restarts after each. */
#define SYS_DO_RESTART        0
#define SYS_DO_FORGET_NETWORK 1 /* the device starts with its setup access point */
#define SYS_DO_FACTORY_RESET  2 /* erases the settings and all application stores */
HOST_SYSTEM_IMPORT(sys_do, int32_t sys_do(int32_t action));
