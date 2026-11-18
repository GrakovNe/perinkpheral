/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 *
 * The base of Perinkpheral OS API: the ABI version, the import and export macros,
 * the return codes every subject shares, and the log. Included by every
 * header of host/.
 */
#pragma once

#include <stdint.h>

#define HOST_API_VERSION 3

/* Every ABI 3 application requires at least 0.1.30. HOST_SINCE_<call>
 * records only a call or contract entry added after that ABI floor, beside
 * its declaration, with the firmware version it will be released in.
 * The build stamps the manifest's "firmware" with the maximum of the ABI
 * floor, the used entries' introduction stamps and an explicit manifest
 * minimum. Older firmware neither installs nor starts the application. */
#define HOST_ABI_3_FIRMWARE_FIRST "0.1.30"
#define HOST_FIRMWARE_FIRST     "0.1.30" /* entries without an introduction stamp */

/* Perinkpheral OS includes this header for its constants. Outside a
 * wasm build the declarations expand to nothing, because Perinkpheral OS defines
 * functions with some of the same names. */
#ifdef __wasm__
#define HOST_IMPORT(name, ...) \
    __attribute__((import_module("host"), import_name(#name))) __VA_ARGS__
#define APP_EXPORT(name, ...) __attribute__((export_name(#name))) __VA_ARGS__
#else
/* A test of the SDK's library may define them first, to declare the
 * imports it stands in for. */
#ifndef HOST_IMPORT
#define HOST_IMPORT(name, ...)
#endif
#ifndef APP_EXPORT
#define APP_EXPORT(name, ...)
#endif
#endif

/* The size of a struct that crosses between the application and Perinkpheral OS, as both sides must
 * lay it out: a change of one is a change of the contract, and the compiler of either side says
 * so. Perinkpheral OS reads these structs as a 32-bit wasm module lays them out. */
#define HOST_LAYOUT(type, size) _Static_assert(sizeof(type) == (size), #type " is laid out as the contract says")

/* Binary representation: wasm32, little endian, two's complement signed
 * integers. int32_t/uint32_t are 4 bytes aligned to 4, int64_t/uint64_t
 * are 8 bytes aligned to 8, char/uint8_t are 1 byte aligned to 1. Pointers
 * are unsigned 32-bit offsets in the module's memory, aligned to 4 in a
 * struct; zero is NULL. Structs use C field order and natural alignment,
 * with trailing padding to their alignment. sdk/abi/3.txt records sizes,
 * alignments, field types and offsets. Call arguments lower pointers and
 * 32-bit integers to wasm i32, and 64-bit integers to wasm i64.
 *
 * Ordinary input bytes are consumed or copied before an import returns;
 * Perinkpheral OS does not retain application pointers. app_data_buffer() and
 * app_strings_buffer() instead return storage valid for the entire loaded
 * instance: do not move or release it. Data passed to a handler is valid
 * only until that handler returns. String-table words are little-endian
 * uint16_t; picture dimensions and bitmap rows are bytes (host/gfx.h).
 *
 * Buffers use byte lengths, without an implicit terminator. Negative
 * lengths/capacities and out-of-memory ranges return HOST_ERR_ARGUMENT.
 * Zero capacity is accepted for buffer reads except time_text(); the
 * destination must still be a valid memory offset. Successful text reads
 * never append NUL, except the fixed string fields of host_answer_info_t.
 * Unless a call says it truncates, insufficient capacity returns
 * HOST_ERR_NO_SPACE and copies nothing. Read/format/measurement errors
 * leave outputs untouched, except file_read(): a storage error can follow
 * a partial read. Storage writes can fail after changing storage; no
 * rollback is promised. If several arguments are invalid, which error is
 * reported first is unspecified. */

/* Return codes of imports that return no value of their own. */
#define HOST_OK               0
#define HOST_ERR_NOT_DRAWING -1 /* drawing call outside app_on_draw() */
#define HOST_ERR_ARGUMENT    -2 /* pointer outside the application memory; unknown font or timer */
#define HOST_ERR_NOT_FOUND   -3 /* kv_get: key not stored; time_local: time not set */
#define HOST_ERR_NO_SPACE    -4 /* kv_get: value larger than the buffer; kv_set: store full */
#define HOST_ERR_FAILED      -5 /* storage failure; a failed write may have changed storage; no rollback is promised */

/* The calls of the system API: an application that imports one says "system": true in its
 * manifest (host/sys.h). */
#ifdef __wasm__
#define HOST_SYSTEM_IMPORT(name, ...) \
    __attribute__((import_module("host"), import_name(#name))) __VA_ARGS__
#else
#define HOST_SYSTEM_IMPORT(name, ...)
#endif

/* ---- Log ---- */

#define HOST_LOG_ERROR 1
#define HOST_LOG_WARN  2
#define HOST_LOG_INFO  3
#define HOST_LOG_DEBUG 4
/* Writes one line to the device log. A longer text is cut. Characters
 * other than printable ASCII, including line breaks, are logged as "?". */
#define HOST_LOG_TEXT_MAX 200
HOST_IMPORT(log_write, void log_write(int32_t level, const char *text, int32_t length));
