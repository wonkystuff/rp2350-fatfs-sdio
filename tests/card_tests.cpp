// SPDX-License-Identifier: MIT
// Copyright (c) 2026 wonkystuff
#include "fake_sdio.hpp"
#include "rp2350_sd_card.hpp"
#include <cstdio>

using namespace sdio_test;

int main()
{
    fresh();
    pin_bias.fill(-1); // Reset pull-downs must not survive board preparation.
    rp2350_sd_card.prepareBus();
    check(!irq_enabled && requests.empty() && !rp2350_sd_card.ready(),
          "startup reserves the bus without card commands or leaving core-0 IRQ1 enabled");
    check(disk_initialize(1) == STA_NOINIT && requests.empty(), "invalid drives never reach hardware");
    check(disk_initialize(0) == 0 && irq_enabled && rp2350_sd_card.sectorCount() == 8388608,
          "mount initializes an SDHC card and decodes its geometry");
    check(modes.back() == SDIO_STANDARD && saw(6, 2) && saw(16, 512) && read_starts[0] == 0,
          "initialization uses four-bit 25 MHz mode and validates the bus by reading only");
    LBA_t sectors = 0;
    WORD sector_size = 0;
    check(disk_ioctl(0, GET_SECTOR_COUNT, &sectors) == RES_OK && sectors == 8388608 &&
          disk_ioctl(0, GET_SECTOR_SIZE, &sector_size) == RES_OK && sector_size == 512,
          "FatFs receives card geometry through diskio");
    const auto previous_commands = requests.size();
    check(disk_status(0) == 0 && requests.size() == previous_commands,
          "ordinary FatFs status checks do not add card polling");
    alignas(4) std::array<uint8_t, 300 * 512 + 4> data{};
    read_starts.clear(); read_counts.clear();
    check(disk_read(0, data.data(), 10, 300) == RES_OK && read_starts == std::vector<uint32_t>({10, 138, 266}) &&
          read_counts == std::vector<uint32_t>({128, 128, 44}) && data[128 * 512] == uint8_t(138 * 17),
          "large reads split at the driver limit and advance both sector and destination");
    check(saw(12, 0) && !read_active && rp2350_sd_card.present(), "multiblock completion stops the card before probing status");
    check(disk_read(0, data.data() + 1, 4, 2) == RES_OK && data[1] == 68 && data[513] == 85,
          "unaligned reads use the bounce buffer without losing sector order");
    const auto transfers = read_starts.size();
    check(disk_read(0, data.data(), 8388607, 2) == RES_ERROR &&
          disk_read(0, data.data(), UINT32_MAX, 1) == RES_ERROR &&
          disk_read(0, nullptr, 0, 1) == RES_PARERR && disk_read(0, data.data(), 0, 0) == RES_PARERR &&
          read_starts.size() == transfers, "bad buffers/counts/ranges fail before DMA");
    receive_error = true;
    check(disk_read(0, data.data(), 10, 2) == RES_ERROR && !read_active, "CRC failure still terminates the card read");
    receive_error = false;
    check(disk_read(0, data.data(), 10, 2) == RES_OK, "a failed refill can retry without remounting");
    response_error = true;
    check(disk_read(0, data.data(), 10, 2) == RES_ERROR && !read_active, "card-reported read errors are not accepted");
    response_error = false;
    status_busy = true;
    check(disk_read(0, data.data(), 10, 2) == RES_ERROR, "failure to leave data state has a bounded timeout");
    status_busy = false;
    card_present = false;
    check(!rp2350_sd_card.present(), "event-driven presence checks detect card removal");
    check(!rp2350_sd_card.ready() && disk_status(0) == STA_NOINIT,
          "confirmed removal invalidates old card geometry and disk readiness");
    requests.clear();
    check(!rp2350_sd_card.present() && saw(8, 0x1aa),
          "an unavailable card is actively probed, not gated by its old ready flag");
    card_present = true;
    requests.clear();
    check(rp2350_sd_card.present() && saw(0, 0) && saw(8, 0x1aa) && !rp2350_sd_card.ready(),
          "reinsertion is detected without pretending the new card is initialized");
    check(!saw(13, 0x12340000) && !saw(41, SDIO_CARD_OCR_MODE),
          "insertion probe uses no stale address or full card initialization");
    check(disk_initialize(0) == 0, "a detected replacement card can be initialized for mounting");
    rp2350_sd_card.end();
    check(disk_status(0) == STA_NOINIT && disk_read(0, data.data(), 0, 1) == RES_NOTRDY,
          "unmounted card cannot serve stale sectors");
    fresh(false);
    check(disk_initialize(0) == 0 && rp2350_sd_card.sectorCount() == 2097152 &&
          disk_read(0, data.data(), 7, 1) == RES_OK && saw(17, 7 * 512),
          "SD-v2 SDSC geometry and byte addressing are handled separately");
    fresh();
    setBits(69, 48, 0x3fffff);
    check(disk_initialize(0) == STA_NOINIT, "capacity beyond 32-bit LBA support is rejected");
    fresh();
    setBits(127, 126, 3);
    check(disk_initialize(0) == STA_NOINIT, "unsupported CSD format is rejected");
    fresh();
    high_capacity = false;
    check(disk_initialize(0) == STA_NOINIT, "CSD/OCR addressing disagreement is rejected");
    fresh();
    init_busy = true;
    check(disk_initialize(0) == STA_NOINIT, "initialization readiness has a bounded timeout");
    fresh();
    card_present = false;
    check(disk_initialize(0) == STA_NOINIT && read_starts.empty(), "no card does not produce a mounted disk");
    card_present = true;
    check(rp2350_sd_card.present() && !rp2350_sd_card.ready() && disk_initialize(0) == 0,
          "a card inserted after an empty-socket startup can be detected and mounted");
    fresh();
    failed_command = 9;
    check(disk_initialize(0) == STA_NOINIT, "failed geometry read rejects the card");
    fresh();
    failed_command = 8;
    failed_command_status = SDIO_ERR_RESPONSE_CODE;
    check(disk_initialize(0) == STA_NOINIT && read_starts.empty(),
          "a wrong CMD8 response tag never advances to geometry or data access");
    fresh();
    receive_error = true;
    check(disk_initialize(0) == STA_NOINIT && !rp2350_sd_card.ready(), "failed startup data check leaves the card unavailable");
    fresh();
    check(disk_initialize(0) == 0, "card can be reinitialized after removal/failure");

    std::puts("Read-only RP2350 SD card protocol scenarios passed");
}
