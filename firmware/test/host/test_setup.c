/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include <string.h>

#include "captive_dns.h"
#include "qrcodegen.h"
#include "wifi_qr.h"

#include "tinytest.h"

/* What the setup screen and the setup access point are made of: the text
 * in the code a phone scans, and the name server that sends the phone to
 * the device's page. */

static void test_wifi_qr_text(void)
{
    char text[WIFI_QR_TEXT_MAX];
    CHECK(wifi_qr_text("Perinkpheral-C7B0", text, sizeof text));
    CHECK_STR(text, "WIFI:T:nopass;S:Perinkpheral-C7B0;;");

    /* what the format reads as its own goes in escaped */
    CHECK(wifi_qr_text("a;b:c,d\"e\\f", text, sizeof text));
    CHECK_STR(text, "WIFI:T:nopass;S:a\\;b\\:c\\,d\\\"e\\\\f;;");

    /* the longest name there is, every character escaped, still fits */
    char longest[33];
    memset(longest, ';', 32);
    longest[32] = '\0';
    CHECK(wifi_qr_text(longest, text, sizeof text));
    CHECK(strlen(text) == 16 + 64 + 2);

    char small[20];
    CHECK(!wifi_qr_text("Perinkpheral-C7B0", small, sizeof small));
    CHECK(!wifi_qr_text("", small, 4));
}

static void test_device_name_fits_the_code(void)
{
    /* the setup screen allows itself a code of version 5 at most */
    char text[WIFI_QR_TEXT_MAX];
    uint8_t modules[qrcodegen_BUFFER_LEN_FOR_VERSION(5)], scratch[sizeof modules];
    CHECK(wifi_qr_text("Perinkpheral-C7B0", text, sizeof text));
    CHECK(qrcodegen_encodeText(text, scratch, modules, qrcodegen_Ecc_MEDIUM, 1, 5,
                               qrcodegen_Mask_AUTO, true));
    CHECK(qrcodegen_getSize(modules) == 29);
}

static const uint8_t DEVICE[4] = { 192, 168, 4, 1 };

/* id 0x1234, recursion asked, one question: example.com, `type`, IN */
static size_t make_query(uint8_t *packet, uint8_t type)
{
    static const uint8_t HEAD[] = { 0x12, 0x34, 0x01, 0x00, 0, 1, 0, 0, 0, 0, 0, 0 };
    static const uint8_t NAME[] = { 7, 'e', 'x', 'a', 'm', 'p', 'l', 'e', 3, 'c', 'o', 'm', 0 };
    memcpy(packet, HEAD, sizeof HEAD);
    memcpy(packet + sizeof HEAD, NAME, sizeof NAME);
    uint8_t *tail = packet + sizeof HEAD + sizeof NAME;
    tail[0] = 0;
    tail[1] = type;
    tail[2] = 0;
    tail[3] = 1;
    return sizeof HEAD + sizeof NAME + 4;
}

static void test_dns_address_question(void)
{
    uint8_t query[64], answer[CAPTIVE_DNS_PACKET_MAX];
    size_t  query_length = make_query(query, 1);
    size_t  length = captive_dns_answer(query, query_length, DEVICE, answer, sizeof answer);
    CHECK(length == query_length + 16);
    CHECK(answer[0] == 0x12 && answer[1] == 0x34);         /* the same id */
    CHECK(answer[2] == 0x85 && answer[3] == 0x80);         /* a response, no error */
    CHECK(answer[5] == 1 && answer[7] == 1);               /* one question, one answer */
    CHECK(memcmp(answer + 12, query + 12, query_length - 12) == 0);
    const uint8_t *record = answer + query_length;
    CHECK(record[0] == 0xC0 && record[1] == 12);           /* the name in the question */
    CHECK(record[3] == 1 && record[5] == 1 && record[11] == 4);
    CHECK(memcmp(record + 12, DEVICE, 4) == 0);
}

static void test_dns_other_questions(void)
{
    uint8_t query[64], answer[CAPTIVE_DNS_PACKET_MAX];
    /* IPv6: there is no such record, and the phone falls back to IPv4 */
    size_t query_length = make_query(query, 28);
    size_t length = captive_dns_answer(query, query_length, DEVICE, answer, sizeof answer);
    CHECK(length == query_length && answer[7] == 0 && (answer[3] & 0x0F) == 0);

    /* what follows the question (EDNS) is dropped, not echoed */
    query_length = make_query(query, 1);
    query[11] = 1;
    memset(query + query_length, 0, 11);
    length = captive_dns_answer(query, query_length + 11, DEVICE, answer, sizeof answer);
    CHECK(length == query_length + 16 && answer[11] == 0);
}

static void test_dns_refuses_garbage(void)
{
    uint8_t query[64], answer[CAPTIVE_DNS_PACKET_MAX];
    size_t  query_length = make_query(query, 1);
    CHECK(captive_dns_answer(query, 5, DEVICE, answer, sizeof answer) == 0);
    /* cut in the middle of the name, and right before the type */
    CHECK(captive_dns_answer(query, 16, DEVICE, answer, sizeof answer) == 0);
    CHECK(captive_dns_answer(query, query_length - 1, DEVICE, answer, sizeof answer) == 0);
    /* no room for the answer */
    CHECK(captive_dns_answer(query, query_length, DEVICE, answer, query_length + 15) == 0);

    query[2] |= 0x80; /* a response, not a question */
    CHECK(captive_dns_answer(query, query_length, DEVICE, answer, sizeof answer) == 0);
    query_length = make_query(query, 1);
    query[5] = 2; /* two questions */
    CHECK(captive_dns_answer(query, query_length, DEVICE, answer, sizeof answer) == 0);
    query_length = make_query(query, 1);
    query[12] = 0xC0; /* a compressed name */
    CHECK(captive_dns_answer(query, query_length, DEVICE, answer, sizeof answer) == 0);
    query_length = make_query(query, 1);
    query[12] = 60; /* a label that runs past the end */
    CHECK(captive_dns_answer(query, query_length, DEVICE, answer, sizeof answer) == 0);
}

int main(void)
{
    test_wifi_qr_text();
    test_device_name_fits_the_code();
    test_dns_address_question();
    test_dns_other_questions();
    test_dns_refuses_garbage();
    REPORT();
}
