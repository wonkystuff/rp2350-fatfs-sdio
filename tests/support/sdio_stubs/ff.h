// SPDX-License-Identifier: MIT
// Copyright (c) 2026 wonkystuff
#pragma once
#include <cstdint>
#define FF_FS_READONLY 1
#define FF_VOLUMES 1
#define FF_LBA64 0
#define FF_MIN_SS 512
#define FF_MAX_SS 512
#define FF_FS_REENTRANT 0
using BYTE = uint8_t;
using WORD = uint16_t;
using DWORD = uint32_t;
using UINT = unsigned;
using LBA_t = uint32_t;

// Minimal FatFs boundary for storage lifecycle tests, not a filesystem model.
enum FRESULT {
    FR_OK, FR_DISK_ERR, FR_INT_ERR, FR_NOT_READY, FR_NO_FILE, FR_NO_PATH,
    FR_INVALID_NAME, FR_DENIED, FR_EXIST, FR_INVALID_OBJECT, FR_WRITE_PROTECTED,
    FR_INVALID_DRIVE, FR_NOT_ENABLED, FR_NO_FILESYSTEM, FR_MKFS_ABORTED,
    FR_TIMEOUT, FR_LOCKED, FR_NOT_ENOUGH_CORE, FR_TOO_MANY_OPEN_FILES,
    FR_INVALID_PARAMETER
};
struct FATFS {};
struct FIL {};
struct DIR {};
struct FILINFO { char fname[1]; };
constexpr BYTE FA_READ = 1;
FRESULT f_mount(FATFS *, const char *, BYTE);
FRESULT f_open(FIL *, const char *, BYTE);
FRESULT f_read(FIL *, void *, UINT, UINT *);
char *f_gets(char *, int, FIL *);
FRESULT f_lseek(FIL *, unsigned long);
unsigned long f_size(FIL *);
unsigned long f_tell(FIL *);
FRESULT f_close(FIL *);
FRESULT f_opendir(DIR *, const char *);
FRESULT f_readdir(DIR *, FILINFO *);
FRESULT f_closedir(DIR *);
