/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "app_delivery.h"
#include "app_scene.h"
#include "app_store.h"
#include "host_api.h"
#include "wm.h"

#include "tinytest.h"

/* An answer from the network on its way to the application it is for: a
 * real module (the probe of test_app) installed in a real folder, a
 * window manager, and a stand-in for the menu. */

static int64_t no_time(void) { return 0; }
static int32_t no_offset(void) { return 0; }
static void no_log(int level, const char *text, int length) { (void)level; (void)text; (void)length; }
static void no_timer(int id, int32_t milliseconds) { (void)id; (void)milliseconds; }
static int32_t no_kv_get(const char *app, const char *key, void *value, int32_t capacity)
{
    (void)app; (void)key; (void)value; (void)capacity;
    return HOST_ERR_NOT_FOUND;
}
static int32_t no_kv_set(const char *app, const char *key, const void *value, int32_t length)
{
    (void)app; (void)key; (void)value; (void)length;
    return HOST_OK;
}
static bool offline(void) { return false; }
static bool no_poll_now(const char *app, int poll) { (void)app; (void)poll; return false; }
static bool no_interval(const char *app, int poll, uint32_t s) { (void)app; (void)poll; (void)s; return false; }
static bool no_alarm(const char *app, uint32_t s) { (void)app; (void)s; return false; }
static int32_t no_file_write(const char *app, const char *file, int32_t offset, const void *bytes,
                             int32_t length)
{
    (void)app; (void)file; (void)offset; (void)bytes; (void)length;
    return HOST_ERR_FAILED;
}
static int32_t no_request(const char *app, const app_request_t *request)
{
    (void)app; (void)request;
    return HOST_ERR_FAILED;
}
static int32_t no_file_size(const char *app, const char *file)
{
    (void)app; (void)file;
    return HOST_ERR_NOT_FOUND;
}
static int32_t no_file_read(const char *app, const char *file, int32_t offset, void *bytes,
                            int32_t capacity)
{
    (void)app; (void)file; (void)offset; (void)bytes; (void)capacity;
    return HOST_ERR_NOT_FOUND;
}
static bool always_dithers(void) { return true; }
static const app_services_t SERVICES = {
    .unix_time = no_time, .time_zone_offset = no_offset, .uptime_ms = no_time, .log = no_log,
    .timer_set = no_timer, .kv_get = no_kv_get, .kv_set = no_kv_set,
    .dithers = always_dithers, .is_online = offline, .poll_now = no_poll_now, .poll_set_interval = no_interval,
    .alarm_set = no_alarm, .http_request = no_request, .file_write = no_file_write,
    .file_size = no_file_size, .file_read = no_file_read,
};

typedef struct {
    scene_t scene;
    int     entered, left;
} menu_t;

static void menu_enter(scene_t *scene) { ((menu_t *)scene)->entered++; }
static void menu_leave(scene_t *scene) { ((menu_t *)scene)->left++; }
static scene_reaction_t menu_button(scene_t *scene, scene_button_t button, scene_press_t press)
{
    (void)scene; (void)button; (void)press;
    scene_reaction_t ignored = { SCENE_IGNORED, NULL };
    return ignored;
}
static void menu_draw(scene_t *scene, gfx_t *surface) { (void)scene; (void)surface; }
static const scene_ops_t MENU_OPS = { .enter = menu_enter, .leave = menu_leave,
                                      .button = menu_button, .draw = menu_draw };
static void no_header(gfx_t *surface, void *context) { (void)surface; (void)context; }

static char                 root[64];
static menu_t               menu;
static scene_t             *probe;
static wm_t                 wm;
static app_delivery_state_t state;

static void copy_file(const char *from, const char *to)
{
    FILE *source = fopen(from, "rb"), *target = fopen(to, "wb");
    if (source == NULL || target == NULL) {
        perror(from);
        exit(1);
    }
    char buffer[4096];
    size_t count;
    while ((count = fread(buffer, 1, sizeof buffer, source)) > 0) {
        fwrite(buffer, 1, count, target);
    }
    fclose(source);
    fclose(target);
}

static void install_probe(void)
{
    strcpy(root, "/tmp/perinkpheral-delivery-XXXXXX");
    if (mkdtemp(root) == NULL) {
        perror("mkdtemp");
        exit(1);
    }
    char path[128];
    snprintf(path, sizeof path, "%s/probe", root);
    mkdir(path, 0755);
    snprintf(path, sizeof path, "%s/probe/app.wasm", root);
    copy_file("build/probe/app.wasm", path);
    snprintf(path, sizeof path, "%s/probe/manifest.json", root);
    copy_file("build/probe/manifest.json", path);

    app_manifest_t manifest;
    CHECK(app_store_list(root, &manifest, 1) == 1);
    probe = app_scene_create(&manifest, root, &SERVICES);
    CHECK(probe != NULL);
}

static void start_on(scene_t *first)
{
    /* What the test before left on the screen goes the way a scene goes. */
    if (app_scene_app(probe) != NULL) {
        probe->ops->leave(probe);
    }
    memset(&menu, 0, sizeof menu);
    menu.scene.ops = &MENU_OPS;
    wm_init(&wm, &menu.scene, first, no_header, NULL);
    wm_shown(&wm);
    app_delivery_init(&state);
}

static void deliver_values(int64_t now_ms)
{
    static const char *const PICKED[] = { "-3.46", "Rain", NULL };
    app_delivery_t delivery = { .kind = APP_DELIVERY_VALUES, .request = 3, .http_status = 200,
                                .values = PICKED, .value_count = 3 };
    app_deliver(&state, &wm, probe, &delivery, now_ms);
}

/* The probe asks for the screen once it has all 2500 bytes of request 1. */
static void deliver_piece(size_t length, int flags, int64_t now_ms)
{
    static uint8_t bytes[HOST_DATA_CHUNK_MAX];
    memset(bytes, 3, sizeof bytes);
    app_delivery_t delivery = { .kind = APP_DELIVERY_PIECE, .request = 1, .http_status = 200,
                                .data = bytes, .length = length, .flags = flags };
    app_deliver(&state, &wm, probe, &delivery, now_ms);
}

static void test_on_the_screen(void)
{
    start_on(probe);
    CHECK(app_scene_app(probe) != NULL);
    deliver_values(0);
    /* the probe asked for a redraw, and nobody pressed anything */
    CHECK(wm_pending_change(&wm) == WM_CHANGED_BY_DATA);
    CHECK(wm.current == probe && state.in_background == NULL);

    /* asking for the screen it already has changes nothing */
    wm_shown(&wm);
    deliver_piece(HOST_DATA_CHUNK_MAX, HOST_DATA_FIRST, 0);
    deliver_piece(HOST_DATA_CHUNK_MAX, 0, 0);
    deliver_piece(2500 - 2048, HOST_DATA_LAST, 0);
    CHECK(wm.current == probe && wm_pending_change(&wm) != WM_CHANGED_BY_SWITCH);
}

static void test_off_the_screen(void)
{
    start_on(&menu.scene);
    CHECK(menu.entered == 1 && app_scene_app(probe) == NULL);
    deliver_values(0);
    /* the menu let go and came back; the probe was loaded only for the call */
    CHECK(menu.left == 1 && menu.entered == 2);
    CHECK(wm.current == &menu.scene && !wm.suspended);
    CHECK(app_scene_app(probe) == NULL && state.in_background == NULL);
}

static void test_asks_for_the_screen(void)
{
    start_on(&menu.scene);
    deliver_piece(HOST_DATA_CHUNK_MAX, HOST_DATA_FIRST, 1000);
    /* between the first piece and the last the owner stays loaded */
    CHECK(wm.suspended && state.in_background == probe && app_scene_app(probe) != NULL);
    CHECK(menu.left == 1 && menu.entered == 1);
    deliver_piece(HOST_DATA_CHUNK_MAX, 0, 1000);
    CHECK(state.in_background == probe);
    deliver_piece(2500 - 2048, HOST_DATA_LAST, 1000);
    /* it asked, and got the screen: the menu does not come back */
    CHECK(wm.current == probe && !wm.suspended && state.in_background == NULL);
    CHECK(wm_pending_change(&wm) == WM_CHANGED_BY_SWITCH);
    CHECK(menu.entered == 1);
    CHECK(app_scene_app(probe) != NULL);
}

static void test_not_twice_in_a_row(void)
{
    start_on(&menu.scene);
    deliver_piece(2500, HOST_DATA_FIRST | HOST_DATA_LAST, 1000); /* too large: not delivered */
    CHECK(wm.current == &menu.scene);

    deliver_piece(HOST_DATA_CHUNK_MAX, HOST_DATA_FIRST, 1000);
    deliver_piece(HOST_DATA_CHUNK_MAX, 0, 1000);
    deliver_piece(2500 - 2048, HOST_DATA_LAST, 1000);
    CHECK(wm.current == probe);

    /* the user goes back to the menu; the next answer comes a moment later */
    wm_button(&wm, SCENE_BUTTON_BACK, SCENE_PRESS_LONG);
    CHECK(wm.current == &menu.scene);
    int64_t too_soon = 1000 + APP_FOCUS_MIN_GAP_MS - 1;
    deliver_piece(HOST_DATA_CHUNK_MAX, HOST_DATA_FIRST, too_soon);
    deliver_piece(HOST_DATA_CHUNK_MAX, 0, too_soon);
    deliver_piece(2500 - 2048, HOST_DATA_LAST, too_soon);
    CHECK(wm.current == &menu.scene && !wm.suspended);

    int64_t late_enough = 1000 + APP_FOCUS_MIN_GAP_MS;
    deliver_piece(HOST_DATA_CHUNK_MAX, HOST_DATA_FIRST, late_enough);
    deliver_piece(HOST_DATA_CHUNK_MAX, 0, late_enough);
    deliver_piece(2500 - 2048, HOST_DATA_LAST, late_enough);
    CHECK(wm.current == probe);
}

static void test_a_request_that_broke_off(void)
{
    start_on(&menu.scene);
    deliver_piece(HOST_DATA_CHUNK_MAX, HOST_DATA_FIRST, 0);
    CHECK(wm.suspended);
    deliver_piece(0, HOST_DATA_FAILED, 0);
    CHECK(!wm.suspended && wm.current == &menu.scene && state.in_background == NULL);
    CHECK(app_scene_app(probe) == NULL);
}

static void test_a_scene_that_is_not_an_application(void)
{
    start_on(&menu.scene);
    CHECK(app_scene_app(&menu.scene) == NULL);
    CHECK(app_scene_begin_background(&menu.scene) == NULL);
    app_scene_end_background(&menu.scene);
    CHECK(app_scene_take_reaction(&menu.scene).kind == SCENE_IGNORED);
}

int main(void)
{
    install_probe();
    test_on_the_screen();
    test_off_the_screen();
    test_asks_for_the_screen();
    test_not_twice_in_a_row();
    test_a_request_that_broke_off();
    test_a_scene_that_is_not_an_application();
    REPORT();
}
