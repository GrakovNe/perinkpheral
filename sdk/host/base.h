/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 *
 * The base of the host API: the ABI version, the import and export macros,
 * the return codes every subject shares, and the log. Included by every
 * header of host/.
 */
#pragma once

#include <stdint.h>

#include <stdint.h>

#define HOST_API_VERSION 2

/* The firmware a call came with, for each call that came after the first
 * firmware or changed its signature since: the build stamps an application
 * with the latest firmware among the calls it makes ("firmware" in its
 * manifest), and an older firmware neither installs nor starts it. A call
 * added or changed gets a line, HOST_SINCE_<call>, beside its declaration in
 * the header of its subject, with the version it will be released in. */
#define HOST_FIRMWARE_FIRST     "0.1.0" /* every call without a line of its own */

/* The host firmware includes this header for its constants. Outside a
 * wasm build the declarations expand to nothing, because the host defines
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

/* The size of a struct that crosses between the application and the host, as both sides must
 * lay it out: a change of one is a change of the contract, and the compiler of either side says
 * so. The host reads these structs as a 32-bit wasm module lays them out. */
#define HOST_LAYOUT(type, size) _Static_assert(sizeof(type) == (size), #type " is laid out as the contract says")

/* Return codes of imports that return no value of their own. */
#define HOST_OK               0
#define HOST_ERR_NOT_DRAWING -1 /* drawing call outside app_on_draw() */
#define HOST_ERR_ARGUMENT    -2 /* pointer outside the application memory; unknown font or timer */
#define HOST_ERR_NOT_FOUND   -3 /* kv_get: key not stored; time_local: time not set */
#define HOST_ERR_NO_SPACE    -4 /* kv_get: value larger than the buffer; kv_set: store full */
#define HOST_ERR_FAILED      -5 /* storage failure; nothing was read or written */

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
HOST_IMPORT(log, void host_log(int32_t level, const char *text, int32_t length));
