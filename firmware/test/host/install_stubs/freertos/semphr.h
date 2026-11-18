/* SPDX-License-Identifier: MIT */
#pragma once
#include "FreeRTOS.h"
typedef bool *SemaphoreHandle_t;
SemaphoreHandle_t xSemaphoreCreateMutex(void);
int xSemaphoreTake(SemaphoreHandle_t semaphore, unsigned wait);
void xSemaphoreGive(SemaphoreHandle_t semaphore);
