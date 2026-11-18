/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe */
#include "answer_headers.h"
#include "host_api.h"
#include "tinytest.h"

int main(void)
{
    answer_headers_t headers;
    answer_headers_reset(&headers);
    answer_headers_add(&headers, "Set-Cookie", "session=one; Path=/; HttpOnly");
    answer_headers_add(&headers, "X-NDM-Realm", "router");
    answer_headers_add(&headers, "set-cookie", "other=two; Expires=Wed, 21 Oct 2030 07:28:00 GMT");
    answer_headers_add(&headers, "X-NDM-Challenge", "0123456789abcdef");
    answer_headers_add(&headers, "Empty", "");
    char value[128] = "unchanged";
    CHECK(answer_headers_get(&headers, "SET-cookie", 10, 0, value, sizeof value) == 29);
    CHECK(memcmp(value, "session=one; Path=/; HttpOnly", 29) == 0);
    CHECK(answer_headers_get(&headers, "set-cookie", 10, 1, value, sizeof value) == 48);
    CHECK(memcmp(value, "other=two; Expires=Wed, 21 Oct 2030 07:28:00 GMT", 48) == 0);
    strcpy(value, "unchanged");
    CHECK(answer_headers_get(&headers, "X-NDM-Realm", 11, 0, value, 5) == HOST_ERR_NO_SPACE);
    CHECK_STR(value, "unchanged");
    CHECK(answer_headers_get(&headers, "missing", 7, 0, value, sizeof value) == HOST_ERR_NOT_FOUND);
    CHECK(answer_headers_get(&headers, "set-cookie", 10, 2, value, sizeof value) == HOST_ERR_NOT_FOUND);
    CHECK(answer_headers_get(&headers, "Empty", 5, 0, NULL, 0) == 0);
    CHECK(answer_headers_get(&headers, "Bad Name", 8, 0, value, sizeof value) == HOST_ERR_ARGUMENT);
    CHECK(answer_headers_get(&headers, "a:b", 3, 0, value, sizeof value) == HOST_ERR_ARGUMENT);
    CHECK(answer_headers_get(&headers, "a\0b", 3, 0, value, sizeof value) == HOST_ERR_ARGUMENT);
    CHECK(answer_headers_get(&headers, "x", 1, -1, value, sizeof value) == HOST_ERR_ARGUMENT);
    CHECK(answer_headers_get(&headers, "x", 1, 0, value, -1) == HOST_ERR_ARGUMENT);
    CHECK(answer_headers_get(&headers, "x", 1, 0, NULL, 1) == HOST_ERR_ARGUMENT);
    CHECK(answer_header_name_valid("!#$%&'*+-.^_`|~09Az", 18));
    char too_large[ANSWER_HEADERS_BYTES + 1];
    memset(too_large, 'x', sizeof too_large - 1);
    too_large[sizeof too_large - 1] = 0;
    answer_headers_add(&headers, "Set-Cookie", too_large);
    answer_headers_add(&headers, "Set-Cookie", "later=must-not-be-number-two");
    CHECK(headers.overflow);
    CHECK(answer_headers_get(&headers, "set-cookie", 10, 2, value, sizeof value) == HOST_ERR_NO_SPACE);
    CHECK(answer_headers_get(&headers, "not-sent", 8, 0, value, sizeof value) == HOST_ERR_NO_SPACE);
    CHECK(answer_headers_get(&headers, "X-NDM-Realm", 11, 0, value, sizeof value) == HOST_ERR_NO_SPACE);
    CHECK(answer_headers_get(&headers, "set-cookie", 10, 0, value, sizeof value) == HOST_ERR_NO_SPACE);
    /* Redirects start a new response, including on a reused connection. */
    answer_headers_reset(&headers);
    CHECK(answer_headers_get(&headers, "set-cookie", 10, 0, value, sizeof value) == HOST_ERR_NOT_FOUND);
    CHECK(!headers.overflow && headers.used == 0);
    for (unsigned i = 0; i < sizeof headers.text; i++) { CHECK(headers.text[i] == 0); }
    answer_headers_add(&headers, "X-New", "yes");
    CHECK(answer_headers_get(&headers, "x-new", 5, 0, value, sizeof value) == 3);
    answer_headers_t reversed;
    answer_headers_reset(&reversed);
    answer_headers_add(&reversed, "A-Filler", too_large);
    answer_headers_add(&reversed, "X-NDM-Realm", "router");
    CHECK(answer_headers_get(&reversed, "X-NDM-Realm", 11, 0, value, sizeof value) == HOST_ERR_NO_SPACE);
    /* Exact capacity stores a whole field; the next field fails closed. */
    answer_headers_reset(&headers);
    too_large[ANSWER_HEADERS_BYTES - 3] = 0;
    answer_headers_add(&headers, "x", too_large);
    CHECK(headers.used == ANSWER_HEADERS_BYTES && !headers.overflow);
    answer_headers_add(&headers, "y", "");
    CHECK(headers.overflow);
    CHECK(sizeof headers == 1028);
    REPORT();
}
