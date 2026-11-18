/* SPDX-License-Identifier: MIT */
#pragma once
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
typedef int portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(lock) ((void)(lock))
#define portEXIT_CRITICAL(lock) ((void)(lock))
#define configASSERT(condition) assert(condition)
#define pdTRUE 1
#define pdFALSE 0
