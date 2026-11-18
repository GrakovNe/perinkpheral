/* SPDX-License-Identifier: MIT */
#pragma once
#include <stddef.h>
#include <stdint.h>
/* Deterministic checksum double: transaction tests exercise matching and
 * mismatching bytes; the production cryptographic primitive is mbedTLS. */
typedef struct { uint32_t state; } mbedtls_sha256_context;
static inline void mbedtls_sha256_init(mbedtls_sha256_context *context) { context->state = 0; }
static inline int mbedtls_sha256_starts(mbedtls_sha256_context *context, int variant) { (void)variant; context->state = 5381; return 0; }
static inline int mbedtls_sha256_update(mbedtls_sha256_context *context, const unsigned char *bytes, size_t length) {
    for (size_t i = 0; i < length; i++) context->state = context->state * 33 + bytes[i];
    return 0;
}
static inline int mbedtls_sha256_finish(mbedtls_sha256_context *context, unsigned char hash[32]) {
    for (unsigned i = 0; i < 32; i++) hash[i] = (unsigned char)(context->state >> ((i % 4) * 8));
    return 0;
}
static inline void mbedtls_sha256_free(mbedtls_sha256_context *context) { (void)context; }
