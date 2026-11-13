/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 *
 * perINKpheral host API: the one contract between the host firmware and
 * the wasm applications. Applications include this header and link the
 * imports; the host builds its import table from the same names.
 *
 * The contract is still moving, and until the first release it is not
 * versioned either: it is simply changed, with no regard for applications
 * built before, and whoever changes it rebuilds the applications.
 * HOST_API_VERSION and the "abi" of a manifest are there for the day that
 * ends; until then the number stays where it is.
 */
#pragma once

#include <stdint.h>

#define HOST_API_VERSION 2

/* The host firmware reads this header too, for the constants. It has
 * functions of its own under some of these names, so outside a wasm build
 * the declarations vanish. */
#ifdef __wasm__
#define HOST_IMPORT(name, ...) \
    __attribute__((import_module("host"), import_name(#name))) __VA_ARGS__
#define APP_EXPORT(name, ...) __attribute__((export_name(#name))) __VA_ARGS__
#else
#define HOST_IMPORT(name, ...)
#define APP_EXPORT(name, ...)
#endif

/* What an import returns when it does not return a value of its own. */
#define HOST_OK               0
#define HOST_ERR_NOT_DRAWING -1 /* a gfx_* call outside app_on_draw() */
#define HOST_ERR_ARGUMENT    -2 /* a pointer outside the memory, an unknown font or timer */
#define HOST_ERR_NOT_FOUND   -3 /* kv_get: nothing is stored under the key; time_local: no time yet */
#define HOST_ERR_NO_SPACE    -4 /* kv_get: the value is larger than the buffer; kv_set: the storage is full */
#define HOST_ERR_FAILED      -5 /* the storage itself failed; nothing was read or written */

/* ---- The application's side: exports, every one optional ---- */

/* Once, right after the module is loaded. The host unloads an application
 * whenever it leaves the screen, so state that must survive goes through
 * the kv_* calls, not into globals. */
APP_EXPORT(app_init, void app_init(void));

/* The application got the screen / is about to lose it. */
APP_EXPORT(app_on_focus, void app_on_focus(void));
APP_EXPORT(app_on_blur, void app_on_blur(void));

/* The last call an instance gets: it is about to be unloaded, whatever
 * the reason - it left the screen, an answer in the background is taken,
 * the device restarts or sleeps, the application is being removed. This is
 * where what must survive is put into the store; app_on_blur() comes
 * first when the application was on the screen, and is not called for one
 * that was not. */
APP_EXPORT(app_on_terminate, void app_on_terminate(void));

/* The four buttons, by what they do in the host's own screens; they sit
 * on the device in this order, and applications are encouraged to keep
 * the meaning. */
#define BTN_UP   1
#define BTN_OK   2
#define BTN_DOWN 3
#define BTN_BACK 4

/* What a button did. A press arrives twice over. As it happens, for an
 * application that follows the finger: KEYDOWN, then KEYUP. And as what
 * it meant: PRESSED right after the key went up, or LONG_PRESSED while
 * it is still held - the release is then only a KEYUP. Most applications
 * handle PRESSED and LONG_PRESSED and ignore the other two. */
#define BTN_KEYDOWN      0
#define BTN_KEYUP        1
#define BTN_PRESSED      2
#define BTN_LONG_PRESSED 3

/* BACK is the way out, and it is the host's unless the application takes
 * it. A short press of BACK leaves an application that has no
 * app_on_back(): the user is back where they came from - the menu, when
 * they opened the application there - and the application writes nothing
 * for it. One with pages of its own exports app_on_back() and gets the
 * press there instead: it goes a page back, and from its first page calls
 * request_exit(). Either way BTN_BACK_PRESSED never reaches
 * app_on_button(); BACK going down and up does, and a long press of BACK
 * is always the host's menu. */
APP_EXPORT(app_on_back, void app_on_back(void));

/* One number says both, and has a name:
 *
 *     void app_on_button(int32_t event)
 *     {
 *         switch (event) {
 *         case BTN_OK_PRESSED:        start();          break;
 *         case BTN_OK_LONG_PRESSED:   reset();          break;
 *         }
 *     }
 *
 * BTN_OF() and BTN_ACTION_OF() take an event apart for code that would
 * rather look at the two separately. BTN_BACK_LONG_PRESSED never arrives:
 * a long press on BACK opens the menu, whatever the application does. */
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

/* A timer set with timer_set() ran out. */
APP_EXPORT(app_on_timer, void app_on_timer(int32_t id));

/* The answer to one of the polls in the manifest, a piece at a time: the
 * host does not keep a whole response in memory. `request` is the poll's
 * number in the manifest. The first piece has HOST_DATA_FIRST and the
 * HTTP status, the last one HOST_DATA_LAST; a short answer is one piece
 * with both. An answer may also simply stop: the instance taking it off
 * the screen is unloaded when somebody presses a button, with no last
 * piece and no failure - app_on_terminate() is then the end of it, the
 * next HOST_DATA_FIRST starts afresh, and a poll cut short this way is
 * asked again in a minute. Nothing half-read is worth keeping.
 * HOST_DATA_FAILED comes alone, with no data: the request
 * could not be made, or broke off, and `http_status` says which - a
 * HOST_FAILED_* below, where a status would be. The data is in the buffer the
 * application gave with app_data_buffer() and is only there until the
 * handler returns.
 *
 * It is called whether the application is on the screen or not; off the
 * screen it is loaded for the delivery and unloaded after it, so what it
 * learns goes into the store, and request_focus() asks for the screen. */
#define HOST_DATA_FIRST     1
#define HOST_DATA_LAST      2
#define HOST_DATA_FAILED    4
#define HOST_DATA_CHUNK_MAX 1024
/* Why there is no answer, where the HTTP status would be - in
 * app_on_data() with HOST_DATA_FAILED, and in app_on_values(). */
#define HOST_FAILED_NO_NETWORK  (-1) /* the device is not on a network */
#define HOST_FAILED_NO_ANSWER   (-2) /* it is, and the server did not answer, or not to the end */
#define HOST_FAILED_NOT_READY   (-3) /* a {{setting}} the request needs is not set */
#define HOST_FAILED_NOT_ALLOWED (-4) /* redirected to a host the manifest's "hosts" does not list */
#define HOST_FAILED_TIMEOUT     (-5) /* the server was reached and did not answer in time */
#define HOST_FAILED_NOT_TRUSTED (-6) /* https, and the server's certificate is not one the device
                                        trusts: its own making, expired, or for another name */
#define HOST_FAILED_TOO_LARGE   (-7) /* the answer is over HOST_ANSWER_MAX bytes */
#define HOST_FAILED_NO_MEMORY   (-8) /* the device has no room for the request just now */
/* The largest answer the host reads, picked apart or handed on in pieces. */
#define HOST_ANSWER_MAX (256 * 1024)
APP_EXPORT(app_on_data, void app_on_data(int32_t request, const uint8_t *data, int32_t length,
                                         int32_t flags, int32_t http_status));

/* The answer to a poll that names what it wants. A poll with "pick" in
 * the manifest - a list of paths into a JSON answer, such as
 * "current.temperature_2m" or "daily.weather_code[0]" - is read by the
 * host, and the application gets this one call instead of the pieces:
 * `request` is the poll's number, and `http_status` a HOST_FAILED_* when
 * there is no answer. Inside the handler data_value() gives the value of
 * path number `index`, as text and not terminated: a number, true, false
 * and null as they were written, a string without its quotes. It answers
 * HOST_ERR_NOT_FOUND for a path the answer did not have and
 * HOST_ERR_NO_SPACE for a buffer that is too small; a value is at most
 * HOST_VALUE_MAX bytes, and one that was longer is cut between characters. sdk/lib reads values as numbers and as text. */
#define HOST_VALUE_MAX 63
APP_EXPORT(app_on_values, void app_on_values(int32_t request, int32_t http_status));
HOST_IMPORT(data_value, int32_t data_value(int32_t index, char *text, int32_t capacity));

/* What is known of the answer beside its data: asked for inside
 * app_on_data() and app_on_values(), about the answer being handed over.
 * It is the same for every piece of one answer. A header the server did
 * not send is an empty text, one that was longer than its room is cut. */
typedef struct host_response {
    int32_t status;            /* the HTTP status, or a HOST_FAILED_* */
    int32_t length;            /* Content-Length; -1 when the server did not say */
    int32_t retry_after;       /* Retry-After in seconds; 0 when there was none, or it was a date */
    char    content_type[48];  /* "application/json; charset=utf-8" */
    char    etag[64];          /* to send back as If-None-Match */
    char    last_modified[32]; /* to send back as If-Modified-Since */
    char    location[128];     /* of a redirect that was not followed */
    char    link[192];         /* where the next page is */
} host_response_t;
HOST_IMPORT(data_response, int32_t data_response(host_response_t *response));

/* Where the host may put a piece of data: HOST_DATA_CHUNK_MAX bytes of the
 * application's memory. sdk/lib has one ready; an application does not
 * write this itself. */
APP_EXPORT(app_data_buffer, uint8_t *app_data_buffer(void));

/* The user changed the application's settings on the device's web page.
 * The settings are declared in the manifest; each is text in the store
 * under its key, read with kv_get(). Called on the screen and off it,
 * like the data handlers. */
APP_EXPORT(app_on_settings, void app_on_settings(void));

/* Paint the whole picture from the application's state. The surface
 * arrives white; gfx_* calls work only in here. The host calls it when
 * the frame is needed, which is after request_redraw() and whenever the
 * host itself has to put the frame together again. Whether the panel is
 * then refreshed, and how, is the host's business: it compares frames. */
APP_EXPORT(app_on_draw, void app_on_draw(void));

/* ---- Drawing, inside app_on_draw() only ---- */

/* The surface is 400 x 300 for a full-screen application and 400 x 260
 * under the host's header otherwise; 0,0 is its top left corner. Ink is
 * black. Anything drawn outside the surface is cut off. */
HOST_IMPORT(gfx_width, int32_t gfx_width(void));
HOST_IMPORT(gfx_height, int32_t gfx_height(void));

/* Text is UTF-8, everywhere in this contract: what an application draws,
 * measures and is handed. Every size has all of printable ASCII, and a
 * character the device has no glyph for is drawn as '?' - an application
 * passes text on as it came and does not cut it by bytes (gfx_text_fit).
 *
 * A font is a size, not a typeface: the typeface is the user's choice, in
 * the device's settings - a monospaced one or one whose letters have their
 * own widths - and the capitals of a size are about as tall in either.
 * So nothing is laid out by counting characters: measure
 * (gfx_text_width, gfx_font_height, gfx_font_ascent, gfx_font_cap_height). */
#define HOST_FONT_TEXT    0 /* the host's own text: capitals about 15 px */
#define HOST_FONT_MEDIUM  1 /* about 25 px */
#define HOST_FONT_LARGE   2 /* about 30 px */
#define HOST_FONT_DISPLAY 3 /* about 50 px */
#define HOST_FONT_HUGE    4 /* about 80 px: five digits fill the width */
#define HOST_FONT_SMALL   5 /* about 9 px: secondary text beside something larger */

/* A number that changes where it stands - a clock, a countdown - should
 * not shift what is beside it, and in a typeface whose digits are of
 * their own widths it does. Add HOST_FONT_TABULAR to the font and every
 * digit takes the room of the widest one:
 *
 *     gfx_text(x, y, HOST_FONT_HUGE | HOST_FONT_TABULAR, "12:07", 5);
 *
 * gfx_text_width() takes the same, and must be asked the same way. What
 * is not a digit keeps its own width. */
#define HOST_FONT_TABULAR 0x100

/* y is the top of the line, not the baseline. Text longer than
 * HOST_TEXT_MAX bytes is cut. Returns the width drawn. */
#define HOST_TEXT_MAX 127
HOST_IMPORT(gfx_text, int32_t gfx_text(int32_t x, int32_t y, int32_t font, const char *text,
                                       int32_t length));
HOST_IMPORT(gfx_text_width, int32_t gfx_text_width(int32_t font, const char *text,
                                                   int32_t length));

/* The text as it fits `max_width` pixels: itself, or cut - between
 * characters, never inside one - with "..." after it. Written into
 * `fitted`, not terminated; returns its length, 0 when not even the dots
 * fit. This is how the host cuts its own. */
HOST_IMPORT(gfx_text_fit, int32_t gfx_text_fit(int32_t font, const char *text, int32_t length,
                                               int32_t max_width, char *fitted,
                                               int32_t capacity));

/* A line of a font, top to bottom: the baseline is gfx_font_ascent()
 * under the top, capitals and digits stand gfx_font_cap_height() high on
 * it, and gfx_font_height() is the whole line with the room for
 * descenders. To put the top of the digits at `top`, draw the text at
 * top - (gfx_font_ascent() - gfx_font_cap_height()). */
HOST_IMPORT(gfx_font_height, int32_t gfx_font_height(int32_t font));
HOST_IMPORT(gfx_font_ascent, int32_t gfx_font_ascent(int32_t font));
HOST_IMPORT(gfx_font_cap_height, int32_t gfx_font_cap_height(int32_t font));

/* What the calls below draw with, until the next gfx_color(): how dark,
 * from HOST_PAPER to HOST_INK. Every draw starts in ink; paper is for
 * text on a filled bar, a hole in a shape. Anything in between is a
 * grey the panel does not have and the host makes by dithering: that
 * many of every sixteen pixels are inked, in a fixed 4 x 4 pattern. A
 * grey is for areas - a fill, a wide bar; thin lines and small text come
 * out broken. */
#define HOST_PAPER 0
#define HOST_INK   16
HOST_IMPORT(gfx_color, int32_t gfx_color(int32_t color));

#define HOST_RECT_OUTLINE 0
#define HOST_RECT_FILLED  1
HOST_IMPORT(gfx_rect, int32_t gfx_rect(int32_t x, int32_t y, int32_t width, int32_t height,
                                       int32_t fill));
HOST_IMPORT(gfx_line, int32_t gfx_line(int32_t x0, int32_t y0, int32_t x1, int32_t y1));

/* bits: row after row, the most significant bit is the left pixel, every
 * row padded to a whole byte. A set bit is drawn in the current colour, a
 * clear one leaves what is there. */
HOST_IMPORT(gfx_bitmap, int32_t gfx_bitmap(int32_t x, int32_t y, int32_t width, int32_t height,
                                           const uint8_t *bits));

/* ---- The scene ---- */

/* The picture is out of date: call app_on_draw(). How soon the panel
 * shows it follows from where this was called: from app_on_button() at
 * once, the user is looking; from app_on_timer() and app_on_data() by the
 * user's e-paper policy, which may be a minute.
 *
 * HOST_REDRAW_SOON: show it now although nobody pressed anything - a
 * countdown that ran out. HOST_REDRAW_CLEAN: a full refresh, which blinks
 * for two or three seconds and leaves no trace of the old picture - for a
 * new image. The host keeps these two a second apart.
 *
 * HOST_REDRAW_NO_BLINK: one picture of a row - show it as soon as the
 * panel can, and never as the full refresh the host makes after every few
 * partial ones. The panel takes about half a second a picture and
 * app_on_draw() its own time on top: three pictures in two seconds at
 * best. Nothing queues up: a timer that runs out while the panel is busy
 * fires once, when it is free, so a timer shorter than the panel runs at
 * the panel's pace. A row that keeps time picks its picture by
 * time_uptime_ms() in app_on_draw() and skips what it missed. The traces
 * of the earlier pictures pile up for as long as the application keeps
 * asking: end the row with HOST_REDRAW_CLEAN, or they stay until the
 * user's own setting calls for a full refresh or the application leaves
 * the screen.
 *
 * None of this is rationed: every refresh costs the battery and wears
 * the panel, and how much of both to spend is the application's call.
 *
 * Which to ask for: HOST_REDRAW_NORMAL for anything a press led to and
 * for data that can wait its turn; HOST_REDRAW_NO_BLINK while the user
 * steps through something - a list, a value - and for a number that
 * turns every second; HOST_REDRAW_SOON for data worth showing now;
 * HOST_REDRAW_CLEAN for a picture that must have no trace of the last.
 *
 * From app_on_alarm(), app_on_intent() and app_on_settings() a redraw is
 * shown as one from app_on_data() is; from app_on_back(), as one from
 * app_on_button(). */
#define HOST_REDRAW_NORMAL   0
#define HOST_REDRAW_SOON     1
#define HOST_REDRAW_CLEAN    2
#define HOST_REDRAW_NO_BLINK 4
HOST_IMPORT(request_redraw, void request_redraw(int32_t flags));

/* The host's header above an application that is not full-screen has two
 * texts: nothing on the left, beside the connection icon, and the date on
 * the right. An application may put its own there - a place, a title - in
 * the host's font and size; one that does not fit is cut with "...", and
 * the two never run into each other. A length of 0 gives a side back to
 * the host; a text of one space leaves it empty. Call it whenever the text changes; it is forgotten when the
 * application leaves the screen. */
#define HOST_HEADER_LEFT     0
#define HOST_HEADER_RIGHT    1
#define HOST_HEADER_TEXT_MAX 31
HOST_IMPORT(header_text, int32_t header_text(int32_t side, const char *text, int32_t length));

/* An icon and a short line that stay in the header when the application
 * is not on the screen: something is going on that the user set going and
 * wants to keep in sight - a countdown, a track playing. The header reads
 * icon text ... text icon: on HOST_HEADER_LEFT the icon stands in place of
 * the host's own with the line after it, on HOST_HEADER_RIGHT the line
 * stands in place of the date with the icon after it. They are there above
 * every screen that has a header, whoever is on it. A side is the
 * application's that set it last. A length of 0 with HOST_ICON_NONE gives
 * the side back - only one's own: when another application has taken it
 * since, nothing changes. Removing the application gives it back too, and
 * so does a restart. Changing it shows on the panel as data does. It may
 * be set from any handler, on the screen or off it. */
#define HOST_STATUS_TEXT_MAX 15
HOST_IMPORT(header_status, int32_t header_status(int32_t side, int32_t icon, const char *text,
                                                 int32_t length));

/* A QR code of `text`, its top left corner at x, y, each of its modules
 * `cell` pixels square; the white a reader wants around it - four modules -
 * is the application's to leave. Returns how many modules a side has, and
 * with a cell of 0 only that, anywhere: for placing the code before it is
 * drawn. HOST_ERR_NO_SPACE for a text too long for the largest code, 57
 * modules a side. */
#define HOST_QR_TEXT_MAX 200
HOST_IMPORT(gfx_qr, int32_t gfx_qr(int32_t x, int32_t y, int32_t cell, const char *text,
                                   int32_t length));

/* The same picture read by the host from one of the application's files,
 * `offset` bytes into it - a file may hold several. A whole frame is
 * 15 KB: more than an application wants to hold, and here it holds none
 * of it. HOST_ERR_NOT_FOUND when there is no such file or it ends before
 * the picture does. */
HOST_IMPORT(gfx_bitmap_file,
            int32_t gfx_bitmap_file(int32_t x, int32_t y, int32_t width, int32_t height,
                                    const char *name, int32_t name_length, int32_t offset));

/* An icon of the host's own set, in the current colour: eleven cells
 * square with its top left corner at (x, y), a cell `cell` pixels - 2 is
 * the size that stands beside the host's text, HOST_ICON_CELL_MAX the
 * largest. HOST_ERR_ARGUMENT for an icon there is none of. */
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

/* ---- The host's own look ----
 *
 * What the host draws for itself, drawn for an application by the host:
 * the same code that draws the menu and the host's questions, so an
 * application that uses it looks the part and stays so when the host's
 * look changes. Inside app_on_draw(), like all drawing. */

/* A list the user moves through with UP and DOWN: a row a line of text,
 * the chosen one white on a black bar, a scroll bar when there are more
 * rows than fit, the chosen row kept in the middle. The application keeps
 * the list - how many rows, which is chosen, where it stands - and calls
 * ui_list_row() for the rows in sight:
 *
 *     static host_list_t list = { .count = 12 };
 *
 *     int32_t first = ui_list_first(&list);
 *     for (int32_t i = first; i < first + ui_list_visible(&list) && i < list.count; i++) {
 *         ui_list_row(&list, i, label, label_length, value, value_length, HOST_ICON_NONE);
 *     }
 *
 * A list has the whole width of the surface and, of its height, what is
 * from `top` for `height` pixels (0: down to the bottom): a ui_title()
 * may stand over it - its return value is the list's `top` - and a
 * ui_actions() row under it, HOST_UI_ACTIONS_HEIGHT less of the height.
 *
 * `selected` -1 is a list nobody chooses from - facts, a row a line: UP
 * and DOWN then move the list itself, and `first` is the row at its top.
 * sdk/lib/app_ui.h has what the buttons do for both kinds.
 *
 * In a row, `value` stands at the right edge, cut to what the label
 * leaves; a length of 0 is none. `icon` is a HOST_ICON_* at the right
 * edge, or HOST_ICON_NONE: HOST_ICON_CHECK marks the one of several that
 * is set. A row out of sight draws nothing. */
#define HOST_ICON_NONE (-1)
typedef struct host_list {
    int32_t count;
    int32_t selected; /* or -1 */
    int32_t top;
    int32_t height;   /* 0: to the bottom of the surface */
    int32_t first;    /* with nothing selected: the row scrolled to the top */
} host_list_t;
HOST_IMPORT(ui_list_visible, int32_t ui_list_visible(const host_list_t *list));
HOST_IMPORT(ui_list_first, int32_t ui_list_first(const host_list_t *list));
HOST_IMPORT(ui_list_row,
            int32_t ui_list_row(const host_list_t *list, int32_t index, const char *label,
                                int32_t label_length, const char *value, int32_t value_length,
                                int32_t icon));

/* A notice on the whole surface, the way the host asks and tells: two
 * lines of large text on the left, a large icon on the right, and along
 * the bottom what the buttons do - "<ok> OK" from the left edge,
 * "BACK <back>" from the right - or, for `is_working`, three strokes that
 * stand for work going on. Every text is terminated; an empty one is not
 * drawn. It is drawn over the surface as it is - which arrives white -
 * and with nothing along the bottom it has the whole height. A question that cannot be taken back should not take a press
 * made before it was on the screen: the host's own wait until it is. */
typedef struct host_notice {
    char    first[32];
    char    second[32];
    char    detail[48]; /* a line of ordinary text under them */
    char    ok[16];     /* what OK does: "Remove" */
    char    back[16];   /* what BACK does: "Cancel" */
    int32_t icon;       /* a HOST_ICON_*, or HOST_ICON_NONE */
    int32_t is_working;
} host_notice_t;
HOST_IMPORT(ui_notice, int32_t ui_notice(const host_notice_t *notice));

/* The parts the host's screens are put together from, for an application
 * to put its own together the same way. Nobody has to: gfx_* draws
 * anything. sdk/lib/app_ui.h adds what the buttons do to the screens that
 * set a value, so that an application writes neither.
 *
 * Text in one of the host's styles, `y` the top of the line, cut with
 * "..." at `max_width` pixels (0: not cut). Returns the width drawn. */
#define HOST_UI_TEXT    0 /* what is read */
#define HOST_UI_TITLE   1 /* what a page is about */
#define HOST_UI_CAPTION 2 /* what stands beside something larger */
#define HOST_UI_VALUE   3 /* the one number the screen is for */
HOST_IMPORT(ui_text, int32_t ui_text(int32_t x, int32_t y, int32_t style, const char *text,
                                     int32_t length, int32_t max_width));

/* How wide the text would be in a style, and how tall a line of it is: to
 * centre a value, to put one thing after another. The styles are the
 * fonts the application has by number too - HOST_UI_TEXT is
 * HOST_FONT_TEXT, HOST_UI_TITLE is HOST_FONT_MEDIUM, HOST_UI_CAPTION is
 * HOST_FONT_SMALL and HOST_UI_VALUE is HOST_FONT_DISPLAY - so everything
 * gfx_font_*() says of a font holds for its style. */
HOST_IMPORT(ui_text_width, int32_t ui_text_width(int32_t style, const char *text,
                                                 int32_t length));
HOST_IMPORT(ui_text_height, int32_t ui_text_height(int32_t style));

/* The host's own screens keep this far from the left and the right edge. */
#define HOST_UI_MARGIN 16

/* The title of a page at its usual place, a line under it. Returns the y
 * where the page's own content begins. */
HOST_IMPORT(ui_title, int32_t ui_title(const char *text, int32_t length));

/* One of the host's icons in one of its sizes. */
#define HOST_UI_ICON_SMALL  0 /* 22 px: beside text */
#define HOST_UI_ICON_MEDIUM 1 /* 44 px */
#define HOST_UI_ICON_LARGE  2 /* 99 px: what a notice is about */
HOST_IMPORT(ui_icon, int32_t ui_icon(int32_t x, int32_t y, int32_t icon, int32_t size));

/* A button of the device and what it does: its name ("UP") white on
 * black and the action beside it - after the name, or, `mirrored`, before
 * it. `x` is the left edge of the whole either way, `y` the top of the
 * text's line. Returns the width; it cannot be asked for without drawing,
 * so what stands against a right edge is the row of ui_actions(). */
HOST_IMPORT(ui_button, int32_t ui_button(int32_t x, int32_t y, const char *name,
                                         int32_t name_length, const char *action,
                                         int32_t action_length, int32_t mirrored));

/* The row along the bottom, HOST_UI_ACTIONS_HEIGHT pixels of the surface:
 * what OK does from the left edge, what BACK does from the right. A
 * length of 0: that button does nothing here. */
#define HOST_UI_ACTIONS_HEIGHT 44
HOST_IMPORT(ui_actions, int32_t ui_actions(const char *ok, int32_t ok_length, const char *back,
                                           int32_t back_length));

/* How much of how much: a frame, filled from the left. It changes when
 * the data does; it is not a thing that moves. */
HOST_IMPORT(ui_bar, int32_t ui_bar(int32_t x, int32_t y, int32_t width, int32_t value,
                                   int32_t maximum));

/* The frame around what UP and DOWN change, with an arrow over it and
 * under it for each way there is to go. */
#define HOST_UI_ARROW_UP   1
#define HOST_UI_ARROW_DOWN 2
HOST_IMPORT(ui_frame, int32_t ui_frame(int32_t x, int32_t y, int32_t width, int32_t height,
                                       int32_t arrows));

/* One of a row of values, on the whole surface: the chosen one framed in
 * the middle, what UP leads to over it and what DOWN leads to under it.
 * An empty text: the row ends there. The row of buttons is drawn too. */
typedef struct host_picker {
    char above[32];
    char chosen[32];
    char below[32];
    char ok[16];
    char back[16];
} host_picker_t;
HOST_IMPORT(ui_picker, int32_t ui_picker(const host_picker_t *picker));

/* A time, a date or both, on the whole surface, the part being set in a
 * frame. */
#define HOST_DATE_HAS_TIME 1
#define HOST_DATE_HAS_DATE 2
#define HOST_DATE_HOUR     0
#define HOST_DATE_MINUTE   1
#define HOST_DATE_DAY      2
#define HOST_DATE_MONTH    3
#define HOST_DATE_YEAR     4
#define HOST_DATE_NONE     5 /* nothing is being set: only shown */
typedef struct host_date {
    int32_t year, month, day; /* month 1..12 */
    int32_t hour, minute;
    int32_t has;              /* HOST_DATE_HAS_* */
    int32_t selected;         /* HOST_DATE_HOUR .. HOST_DATE_NONE */
    char    ok[16];
    char    back[16];
} host_date_t;
HOST_IMPORT(ui_date, int32_t ui_date(const host_date_t *date));

/* ---- Moving about ----
 *
 * What is on the screen is the top of a stack, with the menu under it all.
 * Whatever takes the screen - a question the host asks, an application
 * whose alarm came - goes on top, and when it is done what was under it
 * is there again. A long press of button 4 is the beginning again: the
 * stack is emptied and the menu shows. An application
 * can count on that and on this much more:
 *   - it is unloaded while something covers it, and loaded again when it
 *     is back: app_on_blur() and app_on_terminate() before, app_init() and
 *     app_on_focus() after. What it wants to come back to goes into its
 *     store;
 *   - request_exit() is one step down: to what the application was opened
 *     over, the menu if nothing;
 *   - an application that got the screen with request_focus() is on top
 *     of what the user was looking at, and returns them there when it
 *     exits. */

/* From app_on_data() and app_on_alarm() off the screen: what arrived is
 * worth showing now. The host decides; on the screen it means nothing. */
HOST_IMPORT(request_focus, void request_focus(void));

/* The application is done: one step down the stack, as BACK does in the
 * host's own pages. It takes effect when app_on_button(), app_on_back() or
 * app_on_timer() returns, and is ignored anywhere else. The host's layout has it on a
 * short press of button 4; a long press of button 4 leaves any
 * application without asking it. */
HOST_IMPORT(request_exit, void request_exit(void));

/* One shot after `milliseconds`, then app_on_timer(id); 0 cancels. Setting
 * a running timer restarts it. id: 0..HOST_TIMER_COUNT-1. Timers stop
 * when the application leaves the screen. */
#define HOST_TIMER_COUNT 4
HOST_IMPORT(timer_set, int32_t timer_set(int32_t id, int32_t milliseconds));

/* ---- Storage ---- */

/* A few small values that outlive the application: it is unloaded every
 * time it leaves the screen, and this is where its state waits. Every
 * application has a store of its own. Writing wears the flash, so save
 * when something worth keeping changed - in app_on_terminate(), say - not
 * on every tick. */
#define HOST_KV_KEY_MAX   15  /* 1..15 characters from '!' to '~': no spaces */
#define HOST_KV_VALUE_MAX 1024
#define HOST_KV_TOTAL_MAX 4096 /* all the values of one application together */

/* Returns the length of the value, which is then in `value`. */
HOST_IMPORT(kv_get, int32_t kv_get(const char *key, int32_t key_length, void *value,
                                   int32_t capacity));

/* A length of 0 removes the key. */
HOST_IMPORT(kv_set, int32_t kv_set(const char *key, int32_t key_length, const void *value,
                                   int32_t length));

/* ---- The network ---- */

/* There is no asking whether the network is there: an application asks
 * for what it wants, and "there is no network" is one of the answers
 * (HOST_FAILED_NO_NETWORK). The radio may well be off between requests. */

/* Fetch poll number `poll` of the manifest now instead of when it is due
 * - the user pressed "refresh". The answer arrives the usual way. The
 * host makes one request at a time and not more than one a second, so
 * asking again before the answer changes nothing. */
HOST_IMPORT(poll_now, int32_t poll_now(int32_t poll));

/* A request the application makes itself: an action on a button, a
 * second step after an answer. Only to the hosts the manifest lists
 * under "hosts". The host makes one request at a time for the whole
 * device and keeps at most HOST_HTTP_PENDING_MAX waiting, of all the
 * applications together - HOST_ERR_NO_SPACE when they are taken, to be
 * asked again later; the answer comes through app_on_data() in pieces, under
 * `tag` - whether the application is on the screen by then or not. The
 * tag is the application's own name for the request, a number from
 * HOST_REQUEST_FIRST to HOST_REQUEST_LAST, so that it cannot be taken for
 * a poll:
 *
 *     enum { REQUEST_TOKEN = HOST_REQUEST_FIRST, REQUEST_EVENTS };
 *
 * The instance that gets the answer is not the one that asked - that one
 * was unloaded - and the tag is how it knows what the answer is to.
 *
 * An answer that is JSON is picked apart with sdk/lib/json_pick.h: every
 * piece is fed to a picker, and the values are read after the last one.
 *
 * `headers` is lines of "Name: value" separated by '\n', at most four.
 * {{key}} in the URL and in a header value is replaced by the setting of
 * that name, as in a poll.
 *
 * A GET that is answered with a redirect is asked again where it points,
 * up to three times, and the application sees only the last answer. A
 * redirect out of the manifest's "hosts" fails with
 * HOST_FAILED_NOT_ALLOWED, and the headers given here do not travel to
 * another host. A request with any other method is not sent twice: its
 * 3xx status is the answer. Polls are followed the same way, to any host.
 *
 * Returns HOST_OK, HOST_ERR_ARGUMENT for a URL
 * outside the manifest's hosts or anything malformed, HOST_ERR_NO_SPACE
 * when too many are waiting already. */
#define HOST_HTTP_GET    0
#define HOST_HTTP_POST   1
#define HOST_HTTP_PUT    2
#define HOST_HTTP_DELETE 3
#define HOST_HTTP_PATCH    4
#define HOST_HTTP_HEAD     5 /* the answer is one piece of no data: ask data_response() */
#define HOST_HTTP_PROPFIND 6 /* these two are what a calendar server (CalDAV) is asked with */
#define HOST_HTTP_REPORT   7
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

/* Another pace for poll number `poll` than the manifest says - the
 * server told when to come again. From 10 seconds to a day, counted from
 * now. It is the poll's pace from then on, through a restart too, until
 * it is set again or the application is removed with what it stored. */
HOST_IMPORT(poll_set_interval, int32_t poll_set_interval(int32_t poll, int32_t seconds));

/* ---- Intents ----
 *
 * News of the device an application may want although it is not on the
 * screen. It names the kinds in its manifest -
 *
 *     "intents": ["time_changed", "network_up", "started"]
 *
 * - and gets each through app_on_intent(), loaded for the call if it is
 * not loaded, like an answer from the network. An application that named
 * none is never woken for one.
 *
 * What a handler may count on. app_init() has run before it, as before
 * any handler. The instance is the one on the screen when the application
 * is there, and one loaded for this call and unloaded after it otherwise.
 * HOST_INTENT_STARTED comes once the device is up and has shown its first
 * screen: an application the device reopened after the restart may have
 * been drawn before it, so what app_init() works out must make sense
 * without it. HOST_INTENT_TIME_CHANGED comes too when the device learns
 * the time for the first time, and inside the handler time_now() is the
 * new time already; corrections of a few seconds are not told. */
#define HOST_INTENT_TIME_CHANGED 1 /* the clock or the time zone was set to another: what
                                      was worked out from the time is to be worked out again */
#define HOST_INTENT_NETWORK_UP   2 /* the device is on its network again */
#define HOST_INTENT_STARTED      3 /* the device was powered up or restarted: what a restart
                                      forgets - alarms, a poll's own pace, requests under way -
                                      is set up again from the store. Nobody has to open the
                                      application for it */
APP_EXPORT(app_on_intent, void app_on_intent(int32_t intent));

/* ---- Alarms ---- */

/* Call app_on_alarm(id) once, when the time comes, whether the application
 * is on the screen then or not; off the screen it is loaded for the call
 * and may ask for the screen with request_focus(). An application has
 * HOST_ALARM_COUNT alarms, numbered from 0; setting one again moves it.
 * timer_set() is for the application on the screen; this is for what must
 * happen wherever the user has gone.
 *
 * There are two ways to say when. alarm_set() is "in so many seconds" - a
 * countdown, a second at the least - and does not care what the clock is
 * set to; 0 takes the alarm back. alarm_set_at() is "at this moment" - a time of day, in unix
 * seconds as time_now() counts them - and follows the clock when it is
 * set to another time. Both return HOST_OK, HOST_ERR_ARGUMENT for a number
 * or a time there cannot be, HOST_ERR_NO_SPACE when the device has no room
 * for one more alarm.
 *
 * A restart of the device forgets them: what must survive that is a time
 * in the store, set again in app_init(). A new version of the application
 * does not get the alarms of the old one. */
#define HOST_ALARM_COUNT 4
HOST_IMPORT(alarm_set, int32_t alarm_set(int32_t id, int32_t seconds));
HOST_IMPORT(alarm_set_at, int32_t alarm_set_at(int32_t id, int64_t unix_time));

/* The same alarm again and again: app_on_alarm(id) every `seconds`, until
 * alarm_set(id, 0) takes it back or the alarm is set another way. The
 * first comes `first_in` seconds from now - what is left of the minute in
 * hand, for something that turns on the minute - and 0 is "a whole turn
 * from now". For what has to be kept up while the
 * application is away - the minutes left, in the header (header_status).
 * From a minute to a day: every call loads the application for a moment. */
#define HOST_ALARM_EVERY_MIN 60
#define HOST_ALARM_EVERY_MAX 86400
HOST_IMPORT(alarm_set_every, int32_t alarm_set_every(int32_t id, int32_t seconds,
                                                     int32_t first_in));
APP_EXPORT(app_on_alarm, void app_on_alarm(int32_t id));

/* ---- Files ---- */

/* What does not fit a kv value: a picture, a table, a video. An
 * application reads the files that came in its package - everything in
 * its `files` folder when it was built - and the ones it wrote itself:
 * a picture that arrived in pieces, a cache. All of them together may
 * take HOST_FILES_MAX bytes. Writing wears the flash like kv_set() does. The name is 1-23 of a-z, 0-9,
 * '.', '-' and '_', not starting with a dot. */
#define HOST_FILE_NAME_MAX 23
#define HOST_FILES_MAX     (120 * 1024)

/* The length of the file, or HOST_ERR_NOT_FOUND. */
HOST_IMPORT(file_size, int32_t file_size(const char *name, int32_t name_length));

/* Up to `capacity` bytes from `offset` on; returns how many, which is
 * fewer at the end of the file and 0 past it. */
HOST_IMPORT(file_read, int32_t file_read(const char *name, int32_t name_length, int32_t offset,
                                         void *bytes, int32_t capacity));

/* Writes `length` bytes at `offset`, which is inside the file or right at
 * its end: a file grows piece by piece, and a piece may be written over.
 * A length of 0 at offset 0 removes the file. HOST_ERR_NO_SPACE when the
 * application's files would take more than HOST_FILES_MAX. */
HOST_IMPORT(file_write, int32_t file_write(const char *name, int32_t name_length, int32_t offset,
                                           const void *bytes, int32_t length));

/* ---- Time ---- */

/* Unix seconds; 0 while the device does not know the time.
 *
 * To time something - a countdown, a stopwatch, "last seen" - keep the
 * moment it began as this number, in the store, and work out the rest
 * whenever it is asked for: nothing is counted in ticks, and the
 * application is not loaded most of the time anyway. The clock goes on
 * through a restart of the device and through its sleep, though not
 * through a loss of power. It is 0 only until the network or the user
 * first sets it, and for that while time_uptime_ms() is what there is:
 * it counts through sleep too, and starts from nothing at a restart. */
HOST_IMPORT(time_now, int64_t time_now(void));

/* Seconds to add to time_now() for the user's local time, daylight saving
 * included; 0 while the time is not known. */
HOST_IMPORT(time_zone_offset, int32_t time_zone_offset(void));

/* The user's local time taken apart, so that no application carries a
 * calendar of its own. HOST_ERR_NOT_FOUND while the time is not known. */
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

/* The same for any moment (unix seconds), by the user's zone as its clocks
 * stand at that moment - after the next change to summer time too. For a
 * calendar: what day an event is, what the clock will say. Events that
 * carry a zone of their own are whoever prepares the feed's to convert. */
HOST_IMPORT(time_local_at, int32_t time_local_at(int64_t unix_time, host_local_time_t *parts));

/* And back: the moment at which the user's clock and calendar show this -
 * "07:30 tomorrow" for alarm_set_at(). The weekday is not looked at, and
 * a day or an hour past the end of its month or day counts on into the
 * next: day 32 of January is the first of February. */
HOST_IMPORT(time_from_local, int64_t time_from_local(const host_local_time_t *parts));

/* Milliseconds since the device started; always available. */
HOST_IMPORT(time_uptime_ms, int64_t time_uptime_ms(void));

/* A number that is another after every restart of the device, and never
 * 0. A moment of time_uptime_ms() kept in the store means something only
 * in the run it was taken in: keep this number beside it, and a moment
 * whose number is not this one is of a run that is over. */
HOST_IMPORT(time_boot_id, uint32_t time_boot_id(void));

/* ---- Log ---- */

#define HOST_LOG_ERROR 1
#define HOST_LOG_WARN  2
#define HOST_LOG_INFO  3
#define HOST_LOG_DEBUG 4
/* One line: what is longer is cut, and a character that is not printable
 * ASCII - a line break among them - is logged as "?". */
#define HOST_LOG_TEXT_MAX 200
HOST_IMPORT(log, void host_log(int32_t level, const char *text, int32_t length));

/* ---- the system API ----
 *
 * What the host's own applications are written with: Settings, Device,
 * About. Any application may have it - the device
 * is its owner's - but has to ask: "system": true in its manifest, which
 * the page says aloud before it installs one. A module that imports any
 * of these without having asked is refused. */
#ifdef __wasm__
#define HOST_SYSTEM_IMPORT(name, ...) \
    __attribute__((import_module("host"), import_name(#name))) __VA_ARGS__
#else
#define HOST_SYSTEM_IMPORT(name, ...)
#endif

/* What the device knows about itself, as text: the length, HOST_ERR_NO_SPACE
 * when it does not fit, 0 for what is not known now (no address while it
 * is off the network). */
#define SYS_INFO_VERSION 0 /* of the firmware, as it was built */
#define SYS_INFO_NAME    1 /* "Perinkpheral-C7B0" */
#define SYS_INFO_NETWORK 2 /* the Wi-Fi network it is set up for */
#define SYS_INFO_ADDRESS 3 /* where its page is: its address on the network, or on its own
                              access point while it is not there */
#define SYS_INFO_WIFI    4 /* one of SYS_WIFI_* */
#define SYS_INFO_SLOT    5 /* the firmware slot it runs from: "ota_0" */
#define SYS_INFO_FREE_MEMORY 6 /* of the host, in KB, as digits */
#define SYS_INFO_TIME_ZONE   7 /* as a POSIX TZ string: "MSK-3" */
#define SYS_INFO_JOIN_CODE   8 /* what a QR code says to join a phone to the device's own
                                  access point: "WIFI:T:nopass;S:Perinkpheral-C7B0;;" */
#define SYS_WIFI_ONLINE     "online"
#define SYS_WIFI_CONNECTING "connecting"
#define SYS_WIFI_SETUP      "setup" /* waiting on its own access point to be told a network */
HOST_SYSTEM_IMPORT(sys_info, int32_t sys_info(int32_t what, char *text, int32_t capacity));

/* The mark, "perINKpheraL" in its square font, its top left corner at
 * x, y: `scale` 1 to 5, and 5 is the splash's, 357 px wide. Drawn inside
 * app_on_draw(); how large it is can be asked anywhere. */
#define SYS_LOGO_SCALE_MAX 5
HOST_SYSTEM_IMPORT(sys_logo, int32_t sys_logo(int32_t x, int32_t y, int32_t scale));
HOST_SYSTEM_IMPORT(sys_logo_width, int32_t sys_logo_width(int32_t scale));
HOST_SYSTEM_IMPORT(sys_logo_height, int32_t sys_logo_height(int32_t scale));

/* A setting of the host that is a number, with the range it may be in. */
#define SYS_NUMBER_TIME_AUTOMATIC      0 /* 1: the clock is set from the network; 0: by hand */
#define SYS_NUMBER_REFRESH_FULL_EVERY  1 /* quick refreshes before a full one; 0: always full.
                                            SYS_REFRESH_FULL_NEVER is taken too, outside the range */
#define SYS_NUMBER_REFRESH_MIN_SECONDS 2 /* the least between refreshes nobody asked for */
#define SYS_NUMBER_REFRESH_FORCE_HOURS 3 /* without a refresh before one is forced */
#define SYS_NUMBER_POWER_SAVING        4 /* 0: never; 1: always - the device sleeps when nothing
                                            runs and its radio is on only for a poll and for a
                                            while after a press; 2: the same whenever no
                                            computer is on its USB cable */
#define SYS_NUMBER_REFRESH_FAST        5 /* 1: the shorter full refresh, with less of a blink */
#define SYS_NUMBER_FONT                6 /* the typeface everything is set in: 0 Departure Mono,
                                            1 Jersey */
#define SYS_NUMBER_MENU_IDLE_MINUTES    7 /* the menu left alone so long gives the screen to the
                                            default application */
#define SYS_REFRESH_FULL_NEVER         255
typedef struct {
    int32_t value;
    int32_t least;
    int32_t most;
} sys_number_t;
HOST_SYSTEM_IMPORT(sys_number_get, int32_t sys_number_get(int32_t what, sys_number_t *number));
/* Stored, and in effect at once. HOST_ERR_ARGUMENT for a value the setting does not take. */
HOST_SYSTEM_IMPORT(sys_number_set, int32_t sys_number_set(int32_t what, int32_t value));

/* A setting of the host that is text, of at most SYS_TEXT_MAX bytes. */
#define SYS_TEXT_MAX 63
#define SYS_TEXT_TIME_ZONE 0 /* a POSIX TZ string; read it with sys_info() */
HOST_SYSTEM_IMPORT(sys_text_set, int32_t sys_text_set(int32_t what, const char *text, int32_t length));

/* The clock by hand, in unix seconds. HOST_ERR_FAILED while it is set from
 * the network. */
HOST_SYSTEM_IMPORT(sys_time_set, int32_t sys_time_set(int64_t unix_seconds));

/* The installed applications. Each has an id the host gave it - of its
 * code and its manifest, so that another version is another id - and the
 * list is their ids, in the order the menu has them: how many there are,
 * of which up to `capacity` are written. Removing one takes its files and
 * leaves what it stored; HOST_ERR_NOT_FOUND for an id that is nobody's
 * (any more). */
#define SYS_APP_LIST_MAX 16 /* the most `capacity` may be */
HOST_SYSTEM_IMPORT(sys_app_list, int32_t sys_app_list(uint32_t *ids, int32_t capacity));
HOST_SYSTEM_IMPORT(sys_app_title, int32_t sys_app_title(uint32_t id, char *text, int32_t capacity));
HOST_SYSTEM_IMPORT(sys_app_remove, int32_t sys_app_remove(uint32_t id));

/* The default application: what the device shows by itself - after a
 * restart, when the menu is left alone, and by BACK in the menu. Its id,
 * or 0 when there is none; 0 given takes the choice back. It is kept by
 * name, so a new version of the application is still the choice. */
HOST_SYSTEM_IMPORT(sys_app_default, uint32_t sys_app_default(void));
HOST_SYSTEM_IMPORT(sys_app_set_default, int32_t sys_app_set_default(uint32_t id));

/* What cannot be taken back; the device restarts after each. */
#define SYS_DO_RESTART        0
#define SYS_DO_FORGET_NETWORK 1 /* the device comes back with its setup access point */
#define SYS_DO_FACTORY_RESET  2 /* the settings and what the applications stored are erased */
HOST_SYSTEM_IMPORT(sys_do, int32_t sys_do(int32_t action));
