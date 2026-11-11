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

/* One number says both, and has a name:
 *
 *     void app_on_button(int32_t event)
 *     {
 *         switch (event) {
 *         case BTN_OK_PRESSED:        start();          break;
 *         case BTN_OK_LONG_PRESSED:   reset();          break;
 *         case BTN_BACK_PRESSED:      request_exit();   break;
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
 * with both. HOST_DATA_FAILED comes alone, with no data: the request
 * could not be made, or broke off. The data is in the buffer the
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
APP_EXPORT(app_on_data, void app_on_data(int32_t request, const uint8_t *data, int32_t length,
                                         int32_t flags, int32_t http_status));

/* The answer to a poll that names what it wants. A poll with "pick" in
 * the manifest - a list of paths into a JSON answer, such as
 * "current.temperature_2m" or "daily.weather_code[0]" - is read by the
 * host, and the application gets this one call instead of the pieces:
 * `request` is the poll's number, `http_status` is 0 when the request
 * could not be made. Inside the handler data_value() gives the value of
 * path number `index`, as text and not terminated: a number, true, false
 * and null as they were written, a string without its quotes. It answers
 * HOST_ERR_NOT_FOUND for a path the answer did not have and
 * HOST_ERR_NO_SPACE for a buffer that is too small; a value is at most
 * HOST_VALUE_MAX bytes. sdk/lib reads values as numbers and as text. */
#define HOST_VALUE_MAX 63
APP_EXPORT(app_on_values, void app_on_values(int32_t request, int32_t http_status));
HOST_IMPORT(data_value, int32_t data_value(int32_t index, char *text, int32_t capacity));

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

/* All Departure Mono, a pixel font drawn on an 11 px grid and shown at
 * whole multiples of it; every size has all of printable ASCII. A
 * character outside it is drawn as '?'. */
#define HOST_FONT_TEXT    0 /* 22 px: the host's own text */
#define HOST_FONT_MEDIUM  1 /* 33 px */
#define HOST_FONT_LARGE   2 /* 44 px */
#define HOST_FONT_DISPLAY 3 /* 66 px */
#define HOST_FONT_HUGE    4 /* 110 px: five characters fill the width */
#define HOST_FONT_SMALL   5 /* 11 px: secondary text beside something larger */

/* y is the top of the line, not the baseline. Text longer than
 * HOST_TEXT_MAX bytes is cut. Returns the width drawn. */
#define HOST_TEXT_MAX 127
HOST_IMPORT(gfx_text, int32_t gfx_text(int32_t x, int32_t y, int32_t font, const char *text,
                                       int32_t length));
HOST_IMPORT(gfx_text_width, int32_t gfx_text_width(int32_t font, const char *text,
                                                   int32_t length));

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
 * and leaves no trace of the old picture - for a new image.
 * HOST_REDRAW_NO_BLINK: show it now and do not turn it into the full
 * refresh the host makes after every few partial ones - for pictures that
 * follow one another, where a three-second blink would spoil the row.
 * The traces of the earlier pictures then pile up for as long as the
 * application keeps asking; the panel is cleaned by the first refresh
 * that does not ask, and when the application leaves the screen.
 *
 * None of this is rationed: every refresh costs the battery and wears
 * the panel, and how much of both to spend is the application's call. The
 * host only keeps such refreshes a second apart. */
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

/* A QR code of `text`, its top left corner at x, y, each of its modules
 * `cell` pixels square; the white a reader wants around it - four modules -
 * is the application's to leave. Returns how many modules a side has, and
 * with a cell of 0 only that, anywhere: for placing the code before it is
 * drawn. HOST_ERR_NO_SPACE for a text too long for the largest code, 57
 * modules a side. */
#define HOST_QR_TEXT_MAX 200
HOST_IMPORT(gfx_qr, int32_t gfx_qr(int32_t x, int32_t y, int32_t cell, const char *text,
                                   int32_t length));

/* From app_on_data() off the screen: what arrived is worth showing now.
 * The host decides; on the screen it means nothing. */
HOST_IMPORT(request_focus, void request_focus(void));

/* The application is done: give the screen back, as BACK does in the
 * host's own pages. It takes effect when app_on_button() or app_on_timer()
 * returns, and is ignored anywhere else. The host's layout has it on a
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
 * when something worth keeping changed - in app_on_blur(), say - not on
 * every tick. */
#define HOST_KV_KEY_MAX   15  /* 1..15 characters from '!' to '~': no spaces */
#define HOST_KV_VALUE_MAX 256

/* Returns the length of the value, which is then in `value`. */
HOST_IMPORT(kv_get, int32_t kv_get(const char *key, int32_t key_length, void *value,
                                   int32_t capacity));

/* A length of 0 removes the key. */
HOST_IMPORT(kv_set, int32_t kv_set(const char *key, int32_t key_length, const void *value,
                                   int32_t length));

/* ---- The network ---- */

/* 1 while the device is on its network, 0 otherwise: the difference
 * between "nothing new" and "no way to know". */
HOST_IMPORT(net_is_online, int32_t net_is_online(void));

/* Fetch poll number `poll` of the manifest now instead of when it is due
 * - the user pressed "refresh". The answer arrives the usual way. The
 * host makes one request at a time and not more than one a second, so
 * asking again before the answer changes nothing. */
HOST_IMPORT(poll_now, int32_t poll_now(int32_t poll));

/* A request the application makes itself: an action on a button, a
 * second step after an answer. Only to the hosts the manifest lists
 * under "hosts". The host makes one request at a time for the whole
 * device and keeps at most HOST_HTTP_PENDING_MAX of an application's
 * waiting; the answer comes through app_on_data() in pieces, under the
 * number this returns - HOST_REQUEST_FIRST or more, so that it cannot be
 * taken for a poll - whether the application is on the screen by then or
 * not.
 *
 * `headers` is lines of "Name: value" separated by '\n', at most four.
 * {{key}} in the URL and in a header value is replaced by the setting of
 * that name, as in a poll. Returns HOST_ERR_ARGUMENT for a URL outside
 * the manifest's hosts or anything malformed, HOST_ERR_NO_SPACE when too
 * many are waiting already. */
#define HOST_HTTP_GET    0
#define HOST_HTTP_POST   1
#define HOST_HTTP_PUT    2
#define HOST_HTTP_DELETE 3
#define HOST_HTTP_HEADERS_MAX 255
#define HOST_HTTP_BODY_MAX    512
#define HOST_HTTP_PENDING_MAX 2
#define HOST_REQUEST_FIRST    16
HOST_IMPORT(http_request,
            int32_t http_request(int32_t method, const char *url, int32_t url_length,
                                 const char *headers, int32_t headers_length,
                                 const void *body, int32_t body_length));

/* Another pace for poll number `poll` than the manifest says - the
 * server told when to come again. From 10 seconds to a day, counted from
 * now, until the device restarts. */
HOST_IMPORT(poll_set_interval, int32_t poll_set_interval(int32_t poll, int32_t seconds));

/* ---- The alarm ---- */

/* Call app_on_alarm() once, `seconds` from now, whether the application
 * is on the screen then or not; off the screen it is loaded for the call
 * and may ask for the screen with request_focus(). One alarm for an
 * application: setting it again moves it, 0 takes it back. timer_set()
 * is for the application on the screen; this is for a countdown that
 * must end wherever the user has gone. A restart of the device forgets
 * it: what must survive that is a time in the store, set again in
 * app_init(). */
HOST_IMPORT(alarm_set, int32_t alarm_set(int32_t seconds));
APP_EXPORT(app_on_alarm, void app_on_alarm(void));

/* ---- Files ---- */

/* What does not fit a kv value: a picture, a table, a video. An
 * application reads the files that came in its package - everything in
 * its `files` folder when it was built - and the ones it wrote itself:
 * a picture that arrived in pieces, a cache. All of them together may
 * take HOST_FILES_MAX bytes. Writing wears the flash like kv_set() does. The name is 1-23 of a-z, 0-9,
 * '.', '-' and '_', not starting with a dot. */
#define HOST_FILE_NAME_MAX 23
#define HOST_FILES_MAX     (100 * 1024)

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

/* Unix seconds; 0 while the device does not know the time. */
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

/* Milliseconds since the device started; always available. */
HOST_IMPORT(time_uptime_ms, int64_t time_uptime_ms(void));

/* ---- Log ---- */

#define HOST_LOG_ERROR 1
#define HOST_LOG_WARN  2
#define HOST_LOG_INFO  3
#define HOST_LOG_DEBUG 4
HOST_IMPORT(log, void host_log(int32_t level, const char *text, int32_t length));

/* ---- the system API ----
 *
 * What the host's own applications are written with: About, and later the
 * device's page and its settings. Any application may have it - the device
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
#define SYS_NUMBER_POWER_SAVING        4 /* 1: the device runs from its battery - it sleeps when
                                            nothing runs and its radio is on only for a poll and
                                            for a while after a press; 0: it is plugged in */
#define SYS_REFRESH_FULL_NEVER         255
typedef struct {
    int32_t value;
    int32_t least;
    int32_t most;
} sys_number_t;
HOST_SYSTEM_IMPORT(sys_number_get, int32_t sys_number_get(int32_t what, sys_number_t *number));
/* Stored, and in effect at once. HOST_ERR_ARGUMENT for a value the setting does not take. */
HOST_SYSTEM_IMPORT(sys_number_set, int32_t sys_number_set(int32_t what, int32_t value));

/* A setting of the host that is text. */
#define SYS_TEXT_TIME_ZONE 0 /* a POSIX TZ string; read it with sys_info() */
HOST_SYSTEM_IMPORT(sys_text_set, int32_t sys_text_set(int32_t what, const char *text, int32_t length));

/* The clock by hand, in unix seconds. HOST_ERR_FAILED while it is set from
 * the network. */
HOST_SYSTEM_IMPORT(sys_time_set, int32_t sys_time_set(int64_t unix_seconds));

/* The installed applications by number, from 0: the title of one, or
 * HOST_ERR_NOT_FOUND past the last. Removing one takes its files and
 * leaves what it stored; the numbers are other ones once the handler
 * that removed it has returned. */
HOST_SYSTEM_IMPORT(sys_app_title, int32_t sys_app_title(int32_t index, char *text, int32_t capacity));
HOST_SYSTEM_IMPORT(sys_app_remove, int32_t sys_app_remove(int32_t index));

/* What cannot be taken back; the device restarts after each. */
#define SYS_DO_RESTART        0
#define SYS_DO_FORGET_NETWORK 1 /* the device comes back with its setup access point */
#define SYS_DO_FACTORY_RESET  2 /* the settings and what the applications stored are erased */
HOST_SYSTEM_IMPORT(sys_do, int32_t sys_do(int32_t action));
