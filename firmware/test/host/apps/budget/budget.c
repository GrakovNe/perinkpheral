/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 *
 * The handler budget against calls of the contract: a press makes a
 * hundred of them, fewer yields than the interpreter looks at the clock
 * after, each slow when test_app.c says so. And a load that traps in
 * app_data_buffer while the store has "trap".
 */
#include <stdint.h>

__attribute__((import_module("host"), import_name("log_write"))) void log_write(int32_t level,
                                                                                 const char *text,
                                                                                 int32_t length);
__attribute__((import_module("host"), import_name("kv_get"))) int32_t kv_get(const char *key,
                                                                              int32_t key_length,
                                                                              void *value,
                                                                              int32_t capacity);

static uint8_t data[1024];
static uint8_t strings[8];

__attribute__((export_name("app_data_buffer"))) uint8_t *app_data_buffer(void)
{
    uint8_t flag;
    if (kv_get("trap", 4, &flag, 1) == 1) {
        __builtin_trap();
    }
    return data;
}

__attribute__((export_name("app_strings_buffer"))) uint8_t *app_strings_buffer(void)
{
    log_write(1, "strings", 7);
    return strings;
}

__attribute__((export_name("app_on_button"))) void app_on_button(int32_t event)
{
    (void)event;
    for (int32_t i = 0; i < 100; i++) {
        log_write(1, "x", 1);
    }
}
