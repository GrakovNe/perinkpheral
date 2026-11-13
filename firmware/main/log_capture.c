/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "log_capture.h"

#include <stdarg.h>
#include <stdio.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "log_ring.h"

#define LINE_MAX_BYTES 256
#define ESCAPE         '\x1b'

static char         s_storage[LOG_CAPTURE_BYTES];
static log_ring_t   s_ring;
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static vprintf_like_t s_console;
static SemaphoreHandle_t s_line_lock;
static uint32_t     s_written; /* bytes ever logged: where the log ends, for whoever follows it */

/* The console gets the colours; the copy that is read as text does not.
 * Removes "ESC [ ... m" in place and returns the new length. */
static size_t strip_colours(char *line, size_t length)
{
    size_t kept = 0;
    for (size_t i = 0; i < length; i++) {
        if (line[i] == ESCAPE) {
            while (i < length && line[i] != 'm') {
                i++;
            }
            continue;
        }
        line[kept++] = line[i];
    }
    return kept;
}

static int capture(const char *format, va_list arguments)
{
    va_list for_console;
    va_copy(for_console, arguments);
    int written = s_console(format, for_console);
    va_end(for_console);

    /* The line is put together in one place for every task, not on the
     * stack of whoever logs: a task with 2 KB of stack logs too, and this
     * would be most of it. A line that cannot have the place - a second
     * task logging at the same instant, or an interrupt - goes to the
     * console only. */
    static char line[LINE_MAX_BYTES];
    if (s_line_lock == NULL || xPortInIsrContext() ||
        xSemaphoreTake(s_line_lock, 0) != pdTRUE) {
        return written;
    }
    int length = vsnprintf(line, sizeof line, format, arguments);
    if (length > 0) {
        size_t kept = strip_colours(line, (size_t)length < sizeof line ? (size_t)length
                                                                       : sizeof line - 1);
        portENTER_CRITICAL(&s_lock);
        log_ring_write(&s_ring, line, kept);
        s_written += (uint32_t)kept;
        portEXIT_CRITICAL(&s_lock);
    }
    xSemaphoreGive(s_line_lock);
    return written;
}

void log_capture_start(void)
{
    log_ring_init(&s_ring, s_storage, sizeof s_storage);
    s_line_lock = xSemaphoreCreateMutex();
    s_console = esp_log_set_vprintf(capture);
}

size_t log_capture_read(char *text, size_t text_size, uint32_t *end)
{
    portENTER_CRITICAL(&s_lock);
    size_t length = log_ring_read(&s_ring, text, text_size);
    if (end != NULL) {
        *end = s_written;
    }
    portEXIT_CRITICAL(&s_lock);
    return length;
}
