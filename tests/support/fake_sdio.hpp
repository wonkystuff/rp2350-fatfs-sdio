// SPDX-License-Identifier: MIT
// Copyright (c) 2026 wonkystuff
#pragma once

#include "sdio_rp2350.h"
#include "pico/time.h"
#include "diskio.h"
#include <array>
#include <vector>

// A scripted external driver boundary, not a replacement for the card adapter.
// Application adapter tests can reuse it without importing private state.
namespace sdio_test
{
struct Request { uint8_t command; uint32_t argument; };
extern std::vector<Request> requests;
extern std::vector<uint32_t> read_starts, read_counts;
extern std::vector<rp2350_sdio_mode_t> modes;
extern std::array<uint8_t, 16> csd;
extern uint32_t clock_us, read_sector;
extern bool card_present, high_capacity, receive_error;
extern bool status_busy, read_active, init_busy, response_error;
extern int failed_command;
extern sdio_status_t failed_command_status;
extern unsigned stops;
extern bool irq_enabled;
extern std::array<int, 6> pin_bias;

void check(bool condition, const char *message);
void setBits(unsigned high, unsigned low, uint32_t value);
void fresh(bool sdhc = true);
bool saw(uint8_t command, uint32_t argument);
}
