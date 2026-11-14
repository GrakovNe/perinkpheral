/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "crash_note.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "esp_attr.h"
#include "esp_private/panic_internal.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "riscv/rvruntime-frames.h"
#include "soc/soc.h"

#define CRASH_NOTE_MAGIC   0x43524E31u /* "CRN1" */
#define CRASH_STACK_WORDS  256         /* stack words scanned for code addresses */
#define CRASH_ADDRESSES    24
#define CRASH_TEXT_BYTES   24

typedef struct {
    uint32_t magic;
    uint32_t starts_since;
    uint32_t pc, ra, sp, cause;
    char     reason[CRASH_TEXT_BYTES];
    char     task[CRASH_TEXT_BYTES];
    uint32_t addresses[CRASH_ADDRESSES];
    uint32_t address_count;
} crash_note_t;

static RTC_NOINIT_ATTR crash_note_t s_note;

void __real_esp_panic_handler(panic_info_t *info);

/* Code in RAM: on this chip it shares the address range with data. */
extern const char _iram_text_start[];
extern const char _iram_text_end[];

static bool IRAM_ATTR is_code_address(uint32_t word)
{
    return (word >= SOC_IROM_LOW && word < SOC_IROM_HIGH) ||
           (word >= (uint32_t)_iram_text_start && word < (uint32_t)_iram_text_end);
}

static bool IRAM_ATTR is_stack_address(uint32_t address)
{
    return address >= SOC_DRAM_LOW && address + sizeof(uint32_t) <= SOC_DRAM_HIGH &&
           (address & 3) == 0;
}

static void IRAM_ATTR copy_text(char *to, const char *from)
{
    int i = 0;
    for (; from != NULL && i < CRASH_TEXT_BYTES - 1 && from[i] != '\0'; i++) {
        to[i] = from[i];
    }
    to[i] = '\0';
}

/* Linked with --wrap=esp_panic_handler: runs before the system's handler,
 * with interrupts off, possibly with the flash cache off. Touches RAM only. */
void IRAM_ATTR __wrap_esp_panic_handler(panic_info_t *info)
{
    const RvExcFrame *frame = info != NULL ? (const RvExcFrame *)info->frame : NULL;
    if (frame != NULL) {
        s_note.magic = 0;
        s_note.starts_since = 0;
        s_note.pc = frame->mepc;
        s_note.ra = frame->ra;
        s_note.sp = frame->sp;
        s_note.cause = frame->mcause;
        copy_text(s_note.reason, info->reason);
        TaskHandle_t task = xTaskGetCurrentTaskHandle();
        copy_text(s_note.task, task != NULL ? pcTaskGetName(task) : NULL);
        s_note.address_count = 0;
        for (uint32_t word = 0; word < CRASH_STACK_WORDS && s_note.address_count < CRASH_ADDRESSES;
             word++) {
            uint32_t at = frame->sp + word * sizeof(uint32_t);
            if (!is_stack_address(at)) {
                break;
            }
            uint32_t value = *(const uint32_t *)at;
            if (is_code_address(value)) {
                s_note.addresses[s_note.address_count++] = value;
            }
        }
        s_note.magic = CRASH_NOTE_MAGIC;
    }
    __real_esp_panic_handler(info);
}

void crash_note_init(void)
{
    if (s_note.magic == CRASH_NOTE_MAGIC) {
        s_note.starts_since++;
    }
}

bool crash_note_text(char *text, size_t text_size)
{
    if (s_note.magic != CRASH_NOTE_MAGIC || text_size == 0) {
        return false;
    }
    s_note.reason[CRASH_TEXT_BYTES - 1] = '\0';
    s_note.task[CRASH_TEXT_BYTES - 1] = '\0';
    int length = snprintf(text, text_size, "%u \"%s\" %s pc %08x ra %08x sp %08x cause %x:",
                          (unsigned)s_note.starts_since, s_note.reason, s_note.task,
                          (unsigned)s_note.pc, (unsigned)s_note.ra, (unsigned)s_note.sp,
                          (unsigned)s_note.cause);
    uint32_t count = s_note.address_count < CRASH_ADDRESSES ? s_note.address_count : CRASH_ADDRESSES;
    for (uint32_t i = 0; i < count && length > 0 && (size_t)length + 10 < text_size; i++) {
        length += snprintf(text + length, text_size - (size_t)length, " %08x",
                           (unsigned)s_note.addresses[i]);
    }
    return true;
}
