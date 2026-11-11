/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include <stdatomic.h>
#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/sockets.h"

#include "captive_dns.h"

static const char *TAG = "captive_dns";

#define DNS_PORT         53
#define TASK_STACK_BYTES 3072
#define TASK_PRIORITY    4
#define RETRY_DELAY_MS   5000

static uint8_t     s_address[4];
static atomic_bool s_enabled;

static int open_socket(void)
{
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) {
        return -1;
    }
    struct sockaddr_in local = {
        .sin_family = AF_INET,
        .sin_port = htons(DNS_PORT),
        .sin_addr.s_addr = htonl(INADDR_ANY),
    };
    if (bind(sock, (struct sockaddr *)&local, sizeof local) != 0) {
        close(sock);
        return -1;
    }
    return sock;
}

static void dns_task(void *arg)
{
    (void)arg;
    static uint8_t query[CAPTIVE_DNS_PACKET_MAX], answer[CAPTIVE_DNS_PACKET_MAX];
    for (;;) {
        int sock = open_socket();
        if (sock < 0) {
            ESP_LOGW(TAG, "cannot listen on port %d, trying again", DNS_PORT);
            vTaskDelay(pdMS_TO_TICKS(RETRY_DELAY_MS));
            continue;
        }
        for (;;) {
            struct sockaddr_in from;
            socklen_t          from_length = sizeof from;
            int received = recvfrom(sock, query, sizeof query, 0, (struct sockaddr *)&from,
                                    &from_length);
            if (received < 0) {
                break;
            }
            if (!atomic_load(&s_enabled)) {
                continue; /* on the home network the device is nobody's name server */
            }
            size_t length =
                captive_dns_answer(query, (size_t)received, s_address, answer, sizeof answer);
            if (length > 0) {
                sendto(sock, answer, length, 0, (struct sockaddr *)&from, from_length);
            }
        }
        close(sock);
    }
}

void captive_dns_start(const uint8_t address[4])
{
    memcpy(s_address, address, sizeof s_address);
    if (xTaskCreate(dns_task, "captive_dns", TASK_STACK_BYTES, NULL, TASK_PRIORITY, NULL) !=
        pdPASS) {
        ESP_LOGE(TAG, "no memory for the task; phones will not open the page by themselves");
    }
}

void captive_dns_enable(bool enabled)
{
    atomic_store(&s_enabled, enabled);
}
