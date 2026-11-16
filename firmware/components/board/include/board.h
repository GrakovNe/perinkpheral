/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/* Seeed Studio XIAO ESP32-C6. Pins on the header are configured by the
 * modules that use them (Kconfig); this is what is not on the header and
 * is driven. GPIO9 (BOOT, strapping) and GPIO15 (the user LED) are not. */

#define BOARD_GPIO_RF_SWITCH   3  /* low enables the antenna switch */
#define BOARD_GPIO_RF_SELECT   14 /* low: ceramic antenna, high: U.FL */

/* Antenna switch to the antenna chosen in menuconfig. Before Wi-Fi. */
void board_init(void);

#ifdef __cplusplus
}
#endif
