// SPDX-License-Identifier: MIT
// Copyright (c) 2026 wonkystuff
#pragma once

// Example wiring: CLK GP2, CMD GP3, DAT0..3 GP4..7. Change these and the
// resource assignments for your board; do not share PIO/SM/DMA/IRQ ownership.
#define SDIO_PIO pio1
#define SDIO_SM 0
#define SDIO_GPIO_FUNC GPIO_FUNC_PIO1
#define SDIO_CLK 2
#define SDIO_CMD 3
#define SDIO_D0 4
#define SDIO_D1 5
#define SDIO_D2 6
#define SDIO_D3 7
#define SDIO_DMACH_A 4
#define SDIO_DMACH_B 5
#define SDIO_DMAIRQ_IDX 1
#define SDIO_DMAIRQ DMA_IRQ_1

#define SDIO_USE_SDFAT 0
#define SDIO_SDFAT_PREFETCH_BUFFER 0
#define SDIO_COMMUNICATION_TEST_DO_WRITE 0
#define SDIO_DEFAULT_SPEED SDIO_STANDARD
#define SDIO_TIME_US() time_us_32()
#define SDIO_CMD_TIMEOUT_US 2000
