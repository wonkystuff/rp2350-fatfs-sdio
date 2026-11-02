// SPDX-License-Identifier: MIT
// Copyright (c) 2026 wonkystuff
#include "fake_sdio.hpp"
#include "rp2350_sd_card.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace sdio_test
{
void check(bool condition, const char *message)
{
    if (!condition) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}
std::vector<Request> requests;
std::vector<uint32_t> read_starts, read_counts;
std::vector<rp2350_sdio_mode_t> modes;
std::array<uint8_t, 16> csd;
uint32_t clock_us = 0, read_sector = 0;
bool card_present = true, high_capacity = true, receive_error = false;
bool status_busy = false, read_active = false, init_busy = false, response_error = false;
bool writes_allowed = false, transmit_error = false, transmit_start_error = false;
bool transmit_timeout = false, stop_error = false, status_response_error = false;
uint32_t failed_write_sector = UINT32_MAX, transmit_started_us = 0;
unsigned transmit_busy_polls = 0, transmit_polls = 0;
unsigned programming_polls = 0;
uint32_t write_sector = 0;
std::vector<uint32_t> write_starts;
std::vector<std::array<uint8_t, 512>> written_blocks;
int failed_command = -1;
sdio_status_t failed_command_status = SDIO_ERR_RESPONSE_TIMEOUT;
unsigned stops = 0;
bool irq_enabled = true;
// Model reset pad bias independently of the PIO driver: assigning a pin's
// function does not establish its idle level on the actual board either.
std::array<int, 6> pin_bias{};

void setBits(unsigned high, unsigned low, uint32_t value)
{
    for (unsigned bit = low; bit <= high; ++bit)
    {
        const auto mask = uint8_t{1} << (bit % 8);
        auto &byte = csd[(127 - bit) / 8];
        byte = (byte & ~mask) | (((value >> (bit - low)) & 1U) ? mask : 0);
    }
}

void fresh(bool sdhc)
{
    rp2350_sd_card.end();
    requests.clear(); read_starts.clear(); read_counts.clear(); modes.clear();
    csd.fill(0);
    high_capacity = sdhc;
    card_present = true;
    receive_error = status_busy = init_busy = response_error = read_active = false;
    writes_allowed = transmit_error = transmit_start_error = false;
    transmit_timeout = stop_error = status_response_error = false;
    failed_write_sector = UINT32_MAX;
    transmit_started_us = 0;
    transmit_busy_polls = transmit_polls = 0;
    programming_polls = 0;
    write_starts.clear(); written_blocks.clear();
    failed_command = -1;
    failed_command_status = SDIO_ERR_RESPONSE_TIMEOUT;
    stops = 0;
    clock_us = 0;
    if (sdhc)
    {
        setBits(127, 126, 1);
        setBits(69, 48, 8191); // 4 GiB, 8388608 sectors.
    }
    else
    {
        setBits(83, 80, 9);
        setBits(73, 62, 4095);
        setBits(49, 47, 7); // 1 GiB, byte-addressed SD-v2 SDSC.
    }
}

bool saw(uint8_t command, uint32_t argument)
{
    for (const auto &request : requests)
        if (request.command == command && request.argument == argument) return true;
    return false;
}
}


using namespace sdio_test;

uint32_t time_us_32() { clock_us += 1000; return clock_us; }
void sleep_us(uint64_t amount) { clock_us += static_cast<uint32_t>(amount); }
void gpio_pull_up(unsigned pin)
{
    check(pin >= SDIO_CMD && pin <= SDIO_D3, "only CMD/DAT get pull-ups");
    pin_bias[pin - SDIO_CLK] = 1;
}
void gpio_disable_pulls(unsigned pin)
{
    check(pin == SDIO_CLK, "the driven clock needs no pull");
    pin_bias[pin - SDIO_CLK] = 0;
}
void irq_set_enabled(unsigned irq, bool enabled)
{
    check(irq == DMA_IRQ_1, "SD uses its own IRQ, not audio IRQ0");
    irq_enabled = enabled;
}
rp2350_sdio_timing_t rp2350_sdio_get_timing(rp2350_sdio_mode_t mode) { return {mode}; }
void rp2350_sdio_init(rp2350_sdio_timing_t timing)
{
    check(pin_bias == std::array<int, 6>{0, 1, 1, 1, 1, 1},
          "board establishes CMD/DAT idle-high bias before starting PIO");
    modes.push_back(timing.mode);
    irq_enabled = true;
}
sdio_status_t rp2350_sdio_command_u32(uint8_t index, uint32_t argument, uint32_t *response, uint32_t flags)
{
    requests.push_back({index, argument});
    *response = 0;
    if (!card_present) return SDIO_ERR_RESPONSE_TIMEOUT;
    if (index == failed_command) return failed_command_status;
    switch (index)
    {
    case 8: *response = 0x1aa; break;
    case 41:
        *response = (init_busy ? 0 : 1U << 31) | (high_capacity ? 1U << 30 : 0);
        break;
    case 3: *response = 0x12340000; break;
    case 13:
        check(!read_active, "presence/status commands never interrupt an ongoing multiblock read");
        if (programming_polls) { --programming_polls; *response = 7U << 9; }
        else *response = (1U << 8) | ((status_busy ? 5U : 4U) << 9);
        if (status_response_error) *response |= 1U << 19;
        break;
    case 24:
        check(writes_allowed && (flags & SDIO_FLAG_STOP_CLK), "only explicit writes start transmit with a stopped clock");
        write_sector = high_capacity ? argument : argument / 512;
        if (response_error) *response = 1U << 30;
        break;
    case 17:
    case 18:
        check(flags & SDIO_FLAG_STOP_CLK, "read command stops the clock until DMA is ready");
        read_sector = high_capacity ? argument : argument / 512;
        read_active = index == 18;
        if (response_error) *response = 1U << 30;
        break;
    case 12: read_active = false; break;
    }
    check((writes_allowed || (index != 24 && index != 25)) && index != 32 && index != 33 && index != 38,
          "playback emits no write or erase commands");
    return SDIO_OK;
}
sdio_status_t rp2350_sdio_command(uint8_t index, uint32_t argument, void *data, int size, uint32_t flags)
{
    if (size == 4)
    {
        uint32_t response = 0;
        const auto status = rp2350_sdio_command_u32(index, argument, &response, flags);
        // Like the real driver, failures leave the destination untouched.
        if (status == SDIO_OK)
            for (unsigned i = 0; i < 4; ++i)
                static_cast<uint8_t *>(data)[i] = static_cast<uint8_t>(response >> (24 - i * 8));
        return status;
    }
    requests.push_back({index, argument});
    if (!card_present || index == failed_command) return SDIO_ERR_RESPONSE_TIMEOUT;
    if (data)
    {
        check(size == 16, "CID/CSD responses use exactly 16 bytes");
        if (index == 9) std::memcpy(data, csd.data(), 16);
        else std::memset(data, 0, 16);
    }
    return SDIO_OK;
}
sdio_status_t rp2350_sdio_rx_start(uint8_t *destination, uint32_t count, uint32_t block_size)
{
    check((reinterpret_cast<uintptr_t>(destination) & 3U) == 0 && count <= SDIO_MAX_BLOCKS_PER_REQ &&
          block_size == 512, "DMA receives aligned, bounded 512-byte blocks");
    read_starts.push_back(read_sector);
    read_counts.push_back(count);
    for (uint32_t block = 0; block < count; ++block)
        for (uint32_t byte = 0; byte < 512; ++byte)
            destination[block * 512 + byte] = static_cast<uint8_t>((read_sector + block) * 17 + byte);
    return SDIO_OK;
}
sdio_status_t rp2350_sdio_rx_poll(uint32_t *) { return receive_error ? SDIO_ERR_DATA_CRC : SDIO_OK; }
sdio_status_t rp2350_sdio_stop() { ++stops; return stop_error ? SDIO_ERR_STOP_TIMEOUT : SDIO_OK; }

sdio_status_t rp2350_sdio_tx_start(const uint8_t *source, uint32_t count, uint32_t size)
{
    check(writes_allowed && count == 1 && size == 512 && (reinterpret_cast<uintptr_t>(source) & 3U) == 0,
          "writes use aligned single-sector DMA");
    if (transmit_start_error) return SDIO_ERR_RESPONSE_TIMEOUT;
    transmit_started_us = clock_us;
    write_starts.push_back(write_sector);
    written_blocks.emplace_back();
    std::memcpy(written_blocks.back().data(), source, 512);
    return SDIO_OK;
}
sdio_status_t rp2350_sdio_tx_poll(uint32_t *)
{
    ++transmit_polls;
    // The pinned low-level driver owns the transmit timeout. Model that
    // contract rather than requiring a duplicate timer in the card adapter.
    if (transmit_timeout)
        return static_cast<uint32_t>(time_us_32() - transmit_started_us) >= SDIO_TRANSFER_TIMEOUT_US
            ? SDIO_ERR_DATA_TIMEOUT : SDIO_BUSY;
    if (transmit_busy_polls) { --transmit_busy_polls; return SDIO_BUSY; }
    return transmit_error || write_sector == failed_write_sector ? SDIO_ERR_DATA_CRC : SDIO_OK;
}
