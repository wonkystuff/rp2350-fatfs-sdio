// SPDX-License-Identifier: MIT
// Copyright (c) 2026 wonkystuff
#pragma once
#include "ff.h"
using DSTATUS = uint8_t;
enum DRESULT { RES_OK, RES_ERROR, RES_WRPRT, RES_NOTRDY, RES_PARERR };
constexpr DSTATUS STA_NOINIT = 1;
constexpr BYTE CTRL_SYNC = 0, GET_SECTOR_COUNT = 1, GET_SECTOR_SIZE = 2, GET_BLOCK_SIZE = 3;
extern "C"
{
DSTATUS disk_initialize(BYTE);
DSTATUS disk_status(BYTE);
DRESULT disk_read(BYTE, BYTE *, LBA_t, UINT);
DRESULT disk_ioctl(BYTE, BYTE, void *);
}
