/* SPDX-License-Identifier: MIT */
#pragma once
typedef int esp_err_t;
enum { ESP_OK = 0, ESP_FAIL = -1 };
static inline const char *esp_err_to_name(esp_err_t error)
{
    return error == ESP_OK ? "OK" : "failed";
}
