/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 *
 * Dates of the Gregorian calendar as days since 1970-01-01, and back
 * (H. Hinnant's algorithms): for a moment worked out from a date an
 * answer gives, or a date taken from a count of days.
 */
#pragma once

#include <stdint.h>

/* Days since 1970-01-01 of a date; `month` 1..12. */
int32_t app_days_from_date(int32_t year, int32_t month, int32_t day);

/* The date of a count of days since 1970-01-01. */
void app_date_from_days(int32_t days, int32_t *year, int32_t *month, int32_t *day);
