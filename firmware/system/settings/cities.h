/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 *
 * Made by tools/perinkpheral cities of cities.json; edit that, not this.
 *
 * The time zones the device offers with its buttons, each by a city in it,
 * as POSIX TZ strings with the rules for summer time they have today: the
 * city's English name - the one it is kept by - and its Russian. A zone
 * that is not here is set from the device's web page.
 */
#pragma once

typedef struct {
    const char *name;
    const char *ru;
    const char *zone;
} city_t;

static const city_t CITIES[] = {
    { "Adelaide", "Аделаида", "ACST-9:30ACDT,M10.1.0,M4.1.0/3" },
    { "Almaty", "Алматы", "<+05>-5" },
    { "Amsterdam", "Амстердам", "CET-1CEST,M3.5.0,M10.5.0/3" },
    { "Anchorage", "Анкоридж", "AKST9AKDT,M3.2.0,M11.1.0" },
    { "Athens", "Афины", "EET-2EEST,M3.5.0/3,M10.5.0/4" },
    { "Auckland", "Окленд", "NZST-12NZDT,M9.5.0,M4.1.0/3" },
    { "Baku", "Баку", "<+04>-4" },
    { "Bangkok", "Бангкок", "<+07>-7" },
    { "Beijing", "Пекин", "CST-8" },
    { "Belgrade", "Белград", "CET-1CEST,M3.5.0,M10.5.0/3" },
    { "Berlin", "Берлин", "CET-1CEST,M3.5.0,M10.5.0/3" },
    { "Bogota", "Богота", "<-05>5" },
    { "Brisbane", "Брисбен", "AEST-10" },
    { "Buenos Aires", "Буэнос-Айрес", "<-03>3" },
    { "Cairo", "Каир", "EET-2EEST,M4.5.5/0,M10.5.4/24" },
    { "Chicago", "Чикаго", "CST6CDT,M3.2.0,M11.1.0" },
    { "Delhi", "Дели", "IST-5:30" },
    { "Denver", "Денвер", "MST7MDT,M3.2.0,M11.1.0" },
    { "Dhaka", "Дакка", "<+06>-6" },
    { "Dubai", "Дубай", "<+04>-4" },
    { "Dublin", "Дублин", "GMT0IST,M3.5.0/1,M10.5.0" },
    { "Halifax", "Галифакс", "AST4ADT,M3.2.0,M11.1.0" },
    { "Helsinki", "Хельсинки", "EET-2EEST,M3.5.0/3,M10.5.0/4" },
    { "Hong Kong", "Гонконг", "HKT-8" },
    { "Honolulu", "Гонолулу", "HST10" },
    { "Irkutsk", "Иркутск", "<+08>-8" },
    { "Istanbul", "Стамбул", "<+03>-3" },
    { "Jakarta", "Джакарта", "WIB-7" },
    { "Jerusalem", "Иерусалим", "IST-2IDT,M3.4.4/26,M10.5.0" },
    { "Johannesburg", "Йоханнесбург", "SAST-2" },
    { "Kabul", "Кабул", "<+0430>-4:30" },
    { "Kaliningrad", "Калининград", "EET-2" },
    { "Kamchatka", "Камчатка", "<+12>-12" },
    { "Karachi", "Карачи", "PKT-5" },
    { "Kathmandu", "Катманду", "<+0545>-5:45" },
    { "Krasnoyarsk", "Красноярск", "<+07>-7" },
    { "Kyiv", "Киев", "EET-2EEST,M3.5.0/3,M10.5.0/4" },
    { "Lisbon", "Лиссабон", "WET0WEST,M3.5.0/1,M10.5.0" },
    { "London", "Лондон", "GMT0BST,M3.5.0/1,M10.5.0" },
    { "Los Angeles", "Лос-Анджелес", "PST8PDT,M3.2.0,M11.1.0" },
    { "Madrid", "Мадрид", "CET-1CEST,M3.5.0,M10.5.0/3" },
    { "Magadan", "Магадан", "<+11>-11" },
    { "Mexico City", "Мехико", "CST6" },
    { "Minsk", "Минск", "<+03>-3" },
    { "Moscow", "Москва", "MSK-3" },
    { "Nairobi", "Найроби", "EAT-3" },
    { "New York", "Нью-Йорк", "EST5EDT,M3.2.0,M11.1.0" },
    { "Novosibirsk", "Новосибирск", "<+07>-7" },
    { "Omsk", "Омск", "<+06>-6" },
    { "Paris", "Париж", "CET-1CEST,M3.5.0,M10.5.0/3" },
    { "Perth", "Перт", "AWST-8" },
    { "Phoenix", "Финикс", "MST7" },
    { "Prague", "Прага", "CET-1CEST,M3.5.0,M10.5.0/3" },
    { "Reykjavik", "Рейкьявик", "GMT0" },
    { "Riyadh", "Эр-Рияд", "<+03>-3" },
    { "Rome", "Рим", "CET-1CEST,M3.5.0,M10.5.0/3" },
    { "Samara", "Самара", "<+04>-4" },
    { "Santiago", "Сантьяго", "<-04>4<-03>,M9.1.6/24,M4.1.6/24" },
    { "Sao Paulo", "Сан-Паулу", "<-03>3" },
    { "Seoul", "Сеул", "KST-9" },
    { "Singapore", "Сингапур", "<+08>-8" },
    { "St Petersburg", "Санкт-Петербург", "MSK-3" },
    { "St. John's", "Сент-Джонс", "NST3:30NDT,M3.2.0,M11.1.0" },
    { "Stockholm", "Стокгольм", "CET-1CEST,M3.5.0,M10.5.0/3" },
    { "Sydney", "Сидней", "AEST-10AEDT,M10.1.0,M4.1.0/3" },
    { "Tashkent", "Ташкент", "<+05>-5" },
    { "Tbilisi", "Тбилиси", "<+04>-4" },
    { "Tehran", "Тегеран", "<+0330>-3:30" },
    { "Tokyo", "Токио", "JST-9" },
    { "Toronto", "Торонто", "EST5EDT,M3.2.0,M11.1.0" },
    { "UTC", "UTC", "UTC0" },
    { "Vladivostok", "Владивосток", "<+10>-10" },
    { "Warsaw", "Варшава", "CET-1CEST,M3.5.0,M10.5.0/3" },
    { "Yakutsk", "Якутск", "<+09>-9" },
    { "Yekaterinburg", "Екатеринбург", "<+05>-5" },
    { "Yerevan", "Ереван", "<+04>-4" },
    { "Zurich", "Цюрих", "CET-1CEST,M3.5.0,M10.5.0/3" },
};

#define CITY_COUNT ((int32_t)(sizeof CITIES / sizeof CITIES[0]))
