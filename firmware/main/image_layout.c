/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "image_layout.h"

#include "ota_stream.h"

/* The linker puts this section right after the application's description,
 * at the same place in every image: an update is read that far before a
 * byte of it is written. APP_PARTITION_TABLE comes from the build, a
 * number made of partitions.csv. Nothing in the code needs the bytes to
 * be there, so the linker is told to keep them (CMakeLists.txt). */
const __attribute__((section(".rodata_custom_desc"))) ota_layout_t image_layout = {
    .magic = OTA_LAYOUT_MAGIC,
    .partition_table = APP_PARTITION_TABLE,
};

uint32_t image_layout_partition_table(void)
{
    return image_layout.partition_table;
}
