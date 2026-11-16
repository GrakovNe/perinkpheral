/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "app_days.h"

#define DAYS_PER_ERA     146097 /* four hundred years */
#define DAYS_TO_1970     719468 /* from 0000-03-01 */

int32_t app_days_from_date(int32_t year, int32_t month, int32_t day)
{
    /* The year is counted from March, so that the leap day is its last. */
    year -= month <= 2;
    int32_t era = (year >= 0 ? year : year - 399) / 400;
    int32_t year_of_era = year - era * 400;
    int32_t day_of_year = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
    int32_t day_of_era = year_of_era * 365 + year_of_era / 4 - year_of_era / 100 + day_of_year;
    return era * DAYS_PER_ERA + day_of_era - DAYS_TO_1970;
}

void app_date_from_days(int32_t days, int32_t *year, int32_t *month, int32_t *day)
{
    days += DAYS_TO_1970;
    int32_t era = (days >= 0 ? days : days - (DAYS_PER_ERA - 1)) / DAYS_PER_ERA;
    int32_t day_of_era = days - era * DAYS_PER_ERA;
    int32_t year_of_era =
        (day_of_era - day_of_era / 1460 + day_of_era / 36524 - day_of_era / 146096) / 365;
    int32_t day_of_year = day_of_era - (365 * year_of_era + year_of_era / 4 - year_of_era / 100);
    int32_t shifted_month = (5 * day_of_year + 2) / 153;
    *day = day_of_year - (153 * shifted_month + 2) / 5 + 1;
    *month = shifted_month + (shifted_month < 10 ? 3 : -9);
    *year = year_of_era + era * 400 + (*month <= 2);
}
