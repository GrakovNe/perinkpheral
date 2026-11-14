/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Record of the last panic, kept in RTC memory through the restart: the
 * program counter, the return address, the cause, the task and the code
 * addresses found on its stack. Lost at power-off.
 *
 * Resolve addresses with
 * riscv32-esp-elf-addr2line -pfiaC -e build/perinkpheral.elf <address>... */

/* Counts the start. Call once, early. */
void crash_note_init(void);

/* Writes the record as text: "<starts since> <reason> <task> pc ra sp cause: addresses".
 * Returns false when no panic is recorded. */
bool crash_note_text(char *text, size_t text_size);

#ifdef __cplusplus
}
#endif
