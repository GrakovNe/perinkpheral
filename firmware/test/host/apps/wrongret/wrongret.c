/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 *
 * An export the host reads a result of, declared with a result of 64 bits:
 * the interpreter would write eight bytes where the host keeps four.
 */
#include <stdint.h>

__attribute__((export_name("app_strings_buffer"))) int64_t app_strings_buffer(void)
{
    return 0x1122334455667788;
}
