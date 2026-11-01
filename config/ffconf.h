/* FatFs R0.15 patch 1 configuration, derived from ChaN's configuration sample.
 * See LICENSES/FatFs.txt. All storage access must have one caller/owner.
 */
#pragma once
#define FFCONF_DEF 80286

// Read-only filesystem with directory listing, seek and f_gets support.
#define FF_FS_READONLY 1
#define FF_FS_MINIMIZE 1
#define FF_USE_FIND 0
#define FF_USE_MKFS 0
#define FF_USE_FASTSEEK 0
#define FF_USE_EXPAND 0
#define FF_USE_CHMOD 0
#define FF_USE_LABEL 0
#define FF_USE_FORWARD 0
#define FF_USE_STRFUNC 1
#define FF_PRINT_LLI 1
#define FF_PRINT_FLOAT 1
#define FF_STRF_ENCODE 3

// Fixed long-filename work area, not heap allocation. Single-caller only.
#define FF_CODE_PAGE 932
#define FF_USE_LFN 1
#define FF_MAX_LFN 255
#define FF_LFN_UNICODE 0
#define FF_LFN_BUF 255
#define FF_SFN_BUF 12
#define FF_FS_RPATH 0

// One 32-bit-addressed volume; 512-byte sectors; FAT and exFAT.
#define FF_VOLUMES 1
#define FF_STR_VOLUME_ID 0
#define FF_VOLUME_STRS "SD"
#define FF_MULTI_PARTITION 0
#define FF_MIN_SS 512
#define FF_MAX_SS 512
#define FF_LBA64 0
#define FF_MIN_GPT 0x10000000
#define FF_USE_TRIM 0
#define FF_FS_TINY 0
#define FF_FS_EXFAT 1
#define FF_FS_NORTC 0
#define FF_NORTC_MON 1
#define FF_NORTC_MDAY 1
#define FF_NORTC_YEAR 2022
#define FF_FS_NOFSINFO 0
#define FF_FS_LOCK 0
#define FF_FS_REENTRANT 0
#define FF_FS_TIMEOUT 1000
