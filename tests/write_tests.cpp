// SPDX-License-Identifier: MIT
// Copyright (c) 2026 wonkystuff
#include "fake_sdio.hpp"
#include "rp2350_sd_card.hpp"
#include <cstdio>
#include <cstring>

using namespace sdio_test;

namespace
{
void startCard(bool high_capacity_card = true)
{
    fresh(high_capacity_card);
    check(disk_initialize(0) == 0, "card initializes using reads only");
    writes_allowed = true;
}
}

int main()
{
    alignas(4) std::array<uint8_t, 3 * 512 + 4> data{};
    for (unsigned i = 0; i < data.size(); ++i) data[i] = static_cast<uint8_t>(i * 7 + i / 512);
    const auto original = data;
    fresh();
    rp2350_sd_card.prepareBus();
    check(!rp2350_sd_card.write(0, data.data(), 1) && !rp2350_sd_card.sync(),
          "uninitialized media cannot write or sync");
    startCard();
    programming_polls = 4;
    check(rp2350_sd_card.write(20, data.data(), 3) && programming_polls == 0 &&
          write_starts == std::vector<uint32_t>({20, 21, 22}),
          "each sector waits for programming completion and advances its address");
    for (unsigned i = 0; i < 3; ++i)
        check(std::memcmp(written_blocks[i].data(), data.data() + i * 512, 512) == 0,
              "each write transmits the corresponding source sector");
    check(!saw(25, 20) && !saw(12, 0), "completed single-sector writes need no retained multi-block state");
    transmit_busy_polls = 3;
    const auto polls_before = transmit_polls;
    check(rp2350_sd_card.write(23, data.data(), 1) && transmit_busy_polls == 0 &&
          transmit_polls - polls_before == 4,
          "busy transmit polls are not mistaken for completed writes");
    programming_polls = 3;
    check(rp2350_sd_card.sync() && programming_polls == 0, "sync waits until ready in TRAN state");
    const auto transfers = write_starts.size();
    check(!rp2350_sd_card.write(0, nullptr, 1) && !rp2350_sd_card.write(0, data.data(), 0) &&
          !rp2350_sd_card.write(UINT32_MAX, data.data(), 1) &&
          !rp2350_sd_card.write(rp2350_sd_card.sectorCount() - 1, data.data(), 2) &&
          write_starts.size() == transfers, "invalid write ranges fail before transmitting");

    startCard();
    check(rp2350_sd_card.write(40, data.data() + 1, 2) &&
          std::memcmp(written_blocks[0].data(), data.data() + 1, 512) == 0 &&
          std::memcmp(written_blocks[1].data(), data.data() + 513, 512) == 0 && data == original,
          "unaligned writes use bounded aligned storage without modifying the caller's bytes");
    startCard(false);
    check(rp2350_sd_card.write(7, data.data(), 1) && saw(24, 7 * 512),
          "SDSC writes use byte addresses instead of SDHC sector addresses");

    startCard();
    transmit_error = true;
    check(!rp2350_sd_card.write(10, data.data(), 3) && write_starts.size() == 1 && saw(12, 0),
          "transmit errors trigger cleanup and never continue later sectors or report success");
    startCard();
    failed_write_sector = 11;
    check(!rp2350_sd_card.write(10, data.data(), 3) &&
          write_starts == std::vector<uint32_t>({10, 11}) && saw(12, 0) &&
          std::memcmp(written_blocks[0].data(), data.data(), 512) == 0 &&
          std::memcmp(written_blocks[1].data(), data.data() + 512, 512) == 0,
          "a later sector failure stops the request without retrying or attempting subsequent sectors");
    startCard();
    stop_error = true;
    const auto stops_before = stops;
    check(!rp2350_sd_card.write(10, data.data(), 3) && stops == stops_before + 1 &&
          write_starts == std::vector<uint32_t>({10}),
          "cleanup failure cannot acknowledge a write or continue later sectors");
    startCard();
    transmit_error = true;
    failed_command = 12;
    check(!rp2350_sd_card.write(10, data.data(), 3) && saw(12, 0) &&
          write_starts == std::vector<uint32_t>({10}),
          "failed cancellation remains a write failure and stops later sectors");
    startCard();
    transmit_timeout = true;
    const auto timeout_start = clock_us;
    check(!rp2350_sd_card.write(10, data.data(), 3) &&
          static_cast<uint32_t>(clock_us - timeout_start) >= SDIO_TRANSFER_TIMEOUT_US &&
          static_cast<uint32_t>(clock_us - timeout_start) < SDIO_TRANSFER_TIMEOUT_US + 10000 &&
          write_starts == std::vector<uint32_t>({10}) && saw(12, 0),
          "driver transmit timeout is propagated after bounded polling and cleanup");
    startCard();
    transmit_start_error = true;
    check(!rp2350_sd_card.write(10, data.data(), 1) && write_starts.empty() && saw(12, 0),
          "failed DMA setup still cancels the card's pending write");
    startCard();
    response_error = true;
    check(!rp2350_sd_card.write(10, data.data(), 1) && write_starts.empty(),
          "card-reported command errors never start DMA");
    startCard();
    failed_command = 24;
    check(!rp2350_sd_card.write(10, data.data(), 1) && write_starts.empty(),
          "failed write commands cannot be acknowledged");
    startCard();
    failed_command = 13;
    check(!rp2350_sd_card.write(10, data.data(), 1) && !rp2350_sd_card.sync(),
          "failed completion queries prevent write and sync acknowledgement");
    startCard();
    status_response_error = true;
    check(!rp2350_sd_card.write(10, data.data(), 3) && !rp2350_sd_card.sync() &&
          write_starts == std::vector<uint32_t>({10}),
          "card-reported programming errors prevent success even with READY and TRAN set");
    startCard();
    status_busy = true;
    check(!rp2350_sd_card.write(10, data.data(), 1) && clock_us >= SDIO_TRANSFER_TIMEOUT_US,
          "programming-state timeout is bounded and fails the write");
    check(!rp2350_sd_card.sync(), "a stuck card cannot satisfy sync");
    card_present = false;
    check(!rp2350_sd_card.sync(), "removed media cannot satisfy sync");
    rp2350_sd_card.end();
    std::puts("RP2350 raw-sector write scenarios passed");
}
