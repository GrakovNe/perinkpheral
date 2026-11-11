/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "captive_dns.h"

#include <stdbool.h>
#include <string.h>

#define HEADER_LENGTH      12
#define QUESTION_TAIL      4 /* type and class after the name */
#define LABEL_LENGTH_MAX   63
#define FLAG_RESPONSE      0x80 /* in the first flags byte */
#define FLAG_AUTHORITATIVE 0x04
#define FLAG_RECURSION_ASK 0x01
#define OPCODE_MASK        0x78
#define FLAG_RECURSION_OK  0x80 /* in the second flags byte */
#define TYPE_ADDRESS       1
#define CLASS_INTERNET     1
#define ANSWER_TTL_S       60

static const uint8_t ANSWER_HEAD[] = {
    0xC0, HEADER_LENGTH,              /* the name: the one in the question */
    0x00, TYPE_ADDRESS,
    0x00, CLASS_INTERNET,
    0x00, 0x00, 0x00, ANSWER_TTL_S,
    0x00, 0x04,                       /* four bytes of address follow */
};

static uint16_t read_u16(const uint8_t *bytes)
{
    return (uint16_t)(bytes[0] << 8 | bytes[1]);
}

/* The length of the one question in the packet, 0 if it is malformed. */
static size_t question_length(const uint8_t *query, size_t query_length)
{
    size_t at = HEADER_LENGTH;
    for (;;) {
        if (at >= query_length) {
            return 0;
        }
        uint8_t label_length = query[at];
        if (label_length > LABEL_LENGTH_MAX) {
            return 0; /* a compressed name has no place in a question */
        }
        at += 1 + (size_t)label_length;
        if (label_length == 0) {
            break;
        }
    }
    if (at + QUESTION_TAIL > query_length) {
        return 0;
    }
    return at + QUESTION_TAIL - HEADER_LENGTH;
}

size_t captive_dns_answer(const uint8_t *query, size_t query_length, const uint8_t address[4],
                          uint8_t *answer, size_t capacity)
{
    if (query_length < HEADER_LENGTH) {
        return 0;
    }
    bool is_plain_question = (query[2] & (FLAG_RESPONSE | OPCODE_MASK)) == 0;
    if (!is_plain_question || read_u16(query + 4) != 1) {
        return 0;
    }
    size_t question = question_length(query, query_length);
    if (question == 0) {
        return 0;
    }
    const uint8_t *question_tail = query + HEADER_LENGTH + question - QUESTION_TAIL;
    bool wants_address = read_u16(question_tail) == TYPE_ADDRESS &&
                         read_u16(question_tail + 2) == CLASS_INTERNET;

    size_t length = HEADER_LENGTH + question;
    size_t total = length + (wants_address ? sizeof ANSWER_HEAD + 4 : 0);
    if (total > capacity) {
        return 0;
    }
    /* The id and the question go back as they came; whatever followed the
     * question is dropped. */
    memcpy(answer, query, length);
    answer[2] = (uint8_t)(FLAG_RESPONSE | FLAG_AUTHORITATIVE | (query[2] & FLAG_RECURSION_ASK));
    answer[3] = FLAG_RECURSION_OK;
    memset(answer + 6, 0, HEADER_LENGTH - 6);
    if (wants_address) {
        answer[7] = 1; /* one answer */
        memcpy(answer + length, ANSWER_HEAD, sizeof ANSWER_HEAD);
        memcpy(answer + length + sizeof ANSWER_HEAD, address, 4);
    }
    return total;
}
