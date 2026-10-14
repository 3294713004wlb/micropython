/*
 * FAT filesystem port hooks for the browser-only WebAssembly build.
 *
 * Copyright (c) 2026
 * SPDX-License-Identifier: MIT
 *
 * FatFs needs get_fattime() when FF_FS_NORTC is disabled.  Emscripten
 * supplies the C time library, so no extra JavaScript bridge is required.
 */

#include "py/mpconfig.h"

#if MICROPY_VFS_FAT

#include <time.h>
#include "lib/oofatfs/ff.h"

#if !FF_FS_READONLY && !FF_FS_NORTC

DWORD get_fattime(void) {
    time_t timestamp = time(NULL);
    struct tm *date = localtime(&timestamp);

    // FAT only supports calendar years from 1980 to 2107.
    // Return the earliest valid date if the browser clock is unavailable.
    if (date == NULL || date->tm_year + 1900 < 1980) {
        return (DWORD)(1 << 21) | (DWORD)(1 << 16);
    }

    int year = date->tm_year + 1900;
    if (year > 2107) {
        year = 2107;
    }

    return ((DWORD)(year - 1980) << 25)
        | ((DWORD)(date->tm_mon + 1) << 21)
        | ((DWORD)date->tm_mday << 16)
        | ((DWORD)date->tm_hour << 11)
        | ((DWORD)date->tm_min << 5)
        | ((DWORD)date->tm_sec / 2);
}

#endif // !FF_FS_READONLY && !FF_FS_NORTC
#endif // MICROPY_VFS_FAT
