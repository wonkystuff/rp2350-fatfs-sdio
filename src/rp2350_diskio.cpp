// SPDX-License-Identifier: MIT
// Copyright (c) 2026 wonkystuff
#include "rp2350_sd_card.hpp"
#include "ff.h"
#include "diskio.h"

static_assert(FF_FS_READONLY == 1, "This adapter exposes read-only access only");
static_assert(FF_VOLUMES == 1 && FF_LBA64 == 0, "Only drive 0 and 32-bit sector addresses are supported");
static_assert(FF_MIN_SS == 512 && FF_MAX_SS == 512, "Only 512-byte sectors are supported");
static_assert(FF_FS_REENTRANT == 0, "The application must serialize card/filesystem access");

// One caller owns FatFs and card access. disk_status() issues no card command;
// applications schedule actual presence probes through Rp2350SdCard::present().
extern "C"
{
DSTATUS disk_initialize(BYTE drive)
{
    if (drive != 0) return STA_NOINIT;
    return rp2350_sd_card.begin() ? 0 : STA_NOINIT;
}

DSTATUS disk_status(BYTE drive)
{
    return drive == 0 && rp2350_sd_card.ready() ? 0 : STA_NOINIT;
}

DRESULT disk_read(BYTE drive, BYTE *buffer, LBA_t sector, UINT count)
{
    if (drive != 0 || !buffer || !count) return RES_PARERR;
    if (!rp2350_sd_card.ready()) return RES_NOTRDY;
    return rp2350_sd_card.read(sector, buffer, count) ? RES_OK : RES_ERROR;
}

DRESULT disk_ioctl(BYTE drive, BYTE command, void *buffer)
{
    if (drive != 0) return RES_PARERR;
    if (!rp2350_sd_card.ready()) return RES_NOTRDY;
    switch (command)
    {
    case CTRL_SYNC: return RES_OK; // Reads are synchronous, with no write cache.
    case GET_SECTOR_COUNT:
        if (!buffer) return RES_PARERR;
        *static_cast<LBA_t *>(buffer) = rp2350_sd_card.sectorCount();
        return RES_OK;
    case GET_SECTOR_SIZE:
        if (!buffer) return RES_PARERR;
        *static_cast<WORD *>(buffer) = 512;
        return RES_OK;
    case GET_BLOCK_SIZE:
        if (!buffer) return RES_PARERR;
        *static_cast<DWORD *>(buffer) = 1;
        return RES_OK;
    default: return RES_PARERR;
    }
}
}
