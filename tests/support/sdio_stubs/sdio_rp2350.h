// SPDX-License-Identifier: MIT
// Copyright (c) 2026 wonkystuff
#pragma once
#include <cstdint>
#include "hardware/irq.h"
#include "sdio_rp2350_config.h"

// Stand-in for the MIT driver's public protocol API, not for our card adapter.
// Deliberately no transmit/write API: the playback adapter must never call one.
enum sdio_status_t {
    SDIO_OK = 0, SDIO_BUSY = 1, SDIO_ERR_RESPONSE_TIMEOUT = 2,
    SDIO_ERR_RESPONSE_CODE = 4, SDIO_ERR_DATA_CRC = 6
};
enum rp2350_sdio_mode_t { SDIO_INITIALIZE, SDIO_MMC, SDIO_STANDARD };
struct rp2350_sdio_timing_t { rp2350_sdio_mode_t mode; };
constexpr uint32_t SDIO_FLAG_NO_CRC = 1, SDIO_FLAG_NO_LOGMSG = 2;
constexpr uint32_t SDIO_FLAG_NO_CMD_TAG = 4, SDIO_FLAG_STOP_CLK = 8;
constexpr uint32_t SDIO_MAX_BLOCKS_PER_REQ = 128;
constexpr uint32_t SDIO_CARD_OCR_MODE = (1U << 30) | (1U << 28) | (1U << 20);
constexpr uint32_t SDIO_INIT_TIMEOUT_US = 1000000, SDIO_TRANSFER_TIMEOUT_US = 1000000;
rp2350_sdio_timing_t rp2350_sdio_get_timing(rp2350_sdio_mode_t);
void rp2350_sdio_init(rp2350_sdio_timing_t);
sdio_status_t rp2350_sdio_command_u32(uint8_t, uint32_t, uint32_t *, uint32_t);
sdio_status_t rp2350_sdio_command(uint8_t, uint32_t, void *, int, uint32_t);
sdio_status_t rp2350_sdio_rx_start(uint8_t *, uint32_t, uint32_t);
sdio_status_t rp2350_sdio_rx_poll(uint32_t *);
sdio_status_t rp2350_sdio_stop();
