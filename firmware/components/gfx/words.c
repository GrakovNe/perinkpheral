/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "words.h"

#include <stddef.h>
#include <string.h>

/* A notice's large lines hold about eleven letters and its line of
 * ordinary text sixteen: a translation is written to fit, not to mirror. */
static const char *const ENGLISH[WORD_COUNT] = {
    [WORD_DISMISS] = "Dismiss", [WORD_CANCEL] = "Cancel", [WORD_CLOSE] = "Close",
    [WORD_FORMAT] = "Format", [WORD_SKIP] = "Skip",
    [WORD_STORAGE] = "Storage", [WORD_STORAGE_ERROR] = "error",
    [WORD_UPDATING] = "Updating", [WORD_UPDATING_IN_PROGRESS] = "in progress",
    [WORD_CANNOT_START] = "cannot start", [WORD_CRASHED] = "crashed",
    [WORD_FILE_NOT_FOUND] = "File not found.", [WORD_OUT_OF_MEMORY] = "Out of memory.",
    [WORD_UPDATE_THE_APP] = "Update the app.", [WORD_INVALID_APP] = "Invalid app.",
    [WORD_UPDATE] = "Update", [WORD_UPDATE_FIRMWARE] = "firmware?",
    [WORD_FACTORY] = "Factory", [WORD_FACTORY_RESET] = "reset?", [WORD_RESET] = "Reset",
    [WORD_JOIN] = "Join",
    [WORD_INSTALL] = "Install", [WORD_INSTALL_SYSTEM_APP] = "system app?",
    [WORD_LOADING] = "Loading", [WORD_NOT_SET_UP] = "Not set up",
    [WORD_OPEN_THE_PAGE] = "Open the page", [WORD_NO_NETWORK] = "No network",
    [WORD_NO_ANSWER] = "No answer", [WORD_NOT_ALLOWED] = "Not allowed",
    [WORD_NOT_TRUSTED] = "Not trusted", [WORD_CHECK_THE_CERTIFICATE] = "Check the certificate",
    [WORD_TOO_LARGE] = "Too large", [WORD_NO_MEMORY] = "No memory",
    [WORD_ACCESS_DENIED] = "Access denied", [WORD_CHECK_THE_SETTINGS] = "Check the settings",
    [WORD_NOT_FOUND] = "Not found", [WORD_CHECK_THE_ADDRESS] = "Check the address",
    [WORD_CODE] = "Code",
    [WORD_TODAY] = "Today", [WORD_TOMORROW] = "Tmrw",
    [WORD_SUNDAY] = "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday",
    [WORD_SUN] = "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat",
    [WORD_JANUARY] = "January", "February", "March", "April", "May", "June", "July", "August",
    "September", "October", "November", "December",
    [WORD_JAN] = "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec",
};

static const char *const RUSSIAN[WORD_COUNT] = {
    [WORD_DISMISS] = "Закрыть", [WORD_CANCEL] = "Отмена", [WORD_CLOSE] = "Закрыть",
    [WORD_FORMAT] = "Очистить", [WORD_SKIP] = "Пропустить",
    [WORD_STORAGE] = "Ошибка", [WORD_STORAGE_ERROR] = "хранилища",
    [WORD_UPDATING] = "Выполняется", [WORD_UPDATING_IN_PROGRESS] = "обновление",
    [WORD_CANNOT_START] = "не запускается", [WORD_CRASHED] = "аварийный сбой",
    [WORD_FILE_NOT_FOUND] = "Файл не найден.", [WORD_OUT_OF_MEMORY] = "Мало памяти.",
    [WORD_UPDATE_THE_APP] = "Нужно обновить.", [WORD_INVALID_APP] = "Неверный файл.",
    [WORD_UPDATE] = "Обновить", [WORD_UPDATE_FIRMWARE] = "прошивку?",
    [WORD_FACTORY] = "Сбросить", [WORD_FACTORY_RESET] = "всё?", [WORD_RESET] = "Сбросить",
    [WORD_JOIN] = "Подключить",
    [WORD_INSTALL] = "Установить", [WORD_INSTALL_SYSTEM_APP] = "системное?",
    [WORD_LOADING] = "Загрузка", [WORD_NOT_SET_UP] = "Настройте",
    [WORD_OPEN_THE_PAGE] = "Открыть страницу", [WORD_NO_NETWORK] = "Нет сети",
    [WORD_NO_ANSWER] = "Нет ответа", [WORD_NOT_ALLOWED] = "Запрещено",
    [WORD_NOT_TRUSTED] = "Недоверенный", [WORD_CHECK_THE_CERTIFICATE] = "См. сертификат",
    [WORD_TOO_LARGE] = "Не влезает", [WORD_NO_MEMORY] = "Нет памяти",
    [WORD_ACCESS_DENIED] = "Нет доступа", [WORD_CHECK_THE_SETTINGS] = "См. настройки",
    [WORD_NOT_FOUND] = "Не найдено", [WORD_CHECK_THE_ADDRESS] = "Проверьте адрес",
    [WORD_CODE] = "Код",
    [WORD_TODAY] = "Сегодня", [WORD_TOMORROW] = "Завтра",
    [WORD_SUNDAY] = "Воскресенье", "Понедельник", "Вторник", "Среда", "Четверг", "Пятница",
    "Суббота",
    [WORD_SUN] = "Вс", "Пн", "Вт", "Ср", "Чт", "Пт", "Сб",
    [WORD_JANUARY] = "января", "февраля", "марта", "апреля", "мая", "июня", "июля", "августа",
    "сентября", "октября", "ноября", "декабря",
    [WORD_JAN] = "янв", "фев", "мар", "апр", "мая", "июн", "июл", "авг", "сен", "окт", "ноя", "дек",
};

static const char *const *s_words = ENGLISH;

void words_use(const char *language)
{
    s_words = language != NULL && strcmp(language, "ru") == 0 ? RUSSIAN : ENGLISH;
}

const char *word(word_t id)
{
    if ((unsigned)id >= WORD_COUNT) {
        return "";
    }
    return s_words[id] != NULL ? s_words[id] : ENGLISH[id];
}
