/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/* The host's own words, in the language the device is set to: what it puts
 * on the panel itself - its notices and questions, the names of days and
 * months. English is always there; another language is a table of the
 * same words, and a word it lacks is the English one. Pure. */
typedef enum {
    /* what the buttons do */
    WORD_DISMISS, WORD_CANCEL, WORD_CLOSE, WORD_FORMAT, WORD_SKIP,
    /* notices of the host: a line, or two that are read together */
    WORD_STORAGE, WORD_STORAGE_ERROR,
    WORD_UPDATING, WORD_UPDATING_IN_PROGRESS,
    WORD_CANNOT_START, WORD_CRASHED,
    WORD_FILE_NOT_FOUND, WORD_OUT_OF_MEMORY, WORD_UPDATE_THE_APP, WORD_INVALID_APP,
    /* questions only somebody at the device may answer */
    WORD_UPDATE, WORD_UPDATE_FIRMWARE,
    WORD_FACTORY, WORD_FACTORY_RESET, WORD_RESET,
    WORD_JOIN,
    WORD_INSTALL, WORD_INSTALL_SYSTEM_APP,
    /* why an application has nothing to show */
    WORD_LOADING, WORD_NOT_SET_UP, WORD_OPEN_THE_PAGE, WORD_NO_NETWORK, WORD_NO_ANSWER,
    WORD_NOT_ALLOWED, WORD_NOT_TRUSTED, WORD_CHECK_THE_CERTIFICATE, WORD_TOO_LARGE,
    WORD_NO_MEMORY, WORD_ACCESS_DENIED, WORD_CHECK_THE_SETTINGS, WORD_NOT_FOUND,
    WORD_CHECK_THE_ADDRESS, WORD_CODE,
    /* time */
    WORD_TODAY, WORD_TOMORROW,
    WORD_SUNDAY, /* and the six after it */
    WORD_SUN = WORD_SUNDAY + 7, /* the same, short */
    WORD_JANUARY = WORD_SUN + 7, /* as in "13 November", and the eleven after it */
    WORD_JAN = WORD_JANUARY + 12, /* short */
    WORD_COUNT = WORD_JAN + 12,
} word_t;

/* The language by its code ("en", "ru"); one the host has no words for is
 * English. */
void words_use(const char *language);

const char *word(word_t id);

#ifdef __cplusplus
}
#endif
