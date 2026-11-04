// SPDX-License-Identifier: MIT
// Copyright (c) 2026 wonkystuff
#include "fake_sdio.hpp"
#include "rp2350_sd_card.hpp"
#include <cstdio>

using namespace sdio_test;

namespace
{
constexpr uint32_t kAddress = 0x12340000;
constexpr uint32_t kCardError = 1U << 19;
constexpr uint32_t kReadyTran = (1U << 8) | (4U << 9);
alignas(4) std::array<uint8_t, 1028> data;

void startCard()
{
    fresh();
    check(disk_initialize(0) == 0, "failure scenario starts with initialized media");
    requests.clear();
    read_starts.clear();
    read_counts.clear();
}

void rejectedInitialization()
{
    check(disk_initialize(0) == STA_NOINIT && !rp2350_sd_card.ready() &&
          rp2350_sd_card.sectorCount() == 0 && read_starts.empty(),
          "failed discovery/configuration never exposes geometry or starts DMA");
}
}

int main()
{
    fresh();
    rp2350_sd_card.prepareBus();
    check(disk_status(1) == STA_NOINIT && disk_ioctl(1, CTRL_SYNC, nullptr) == RES_PARERR &&
          disk_ioctl(0, CTRL_SYNC, nullptr) == RES_NOTRDY &&
          disk_read(1, data.data(), 0, 1) == RES_PARERR && requests.empty(),
          "invalid drives and unavailable ioctls never issue card commands");
    check(!rp2350_sd_card.read(0, data.data(), 1), "direct read rejects unavailable media");
    startCard();
    check(!rp2350_sd_card.read(0, nullptr, 1) && !rp2350_sd_card.read(0, data.data(), 0) &&
          requests.empty(), "direct read validates buffers/counts without relying on diskio");
    DWORD block_size = 0;
    check(disk_ioctl(0, CTRL_SYNC, nullptr) == RES_OK &&
          disk_ioctl(0, GET_BLOCK_SIZE, &block_size) == RES_OK && block_size == 1 &&
          disk_ioctl(0, GET_SECTOR_COUNT, nullptr) == RES_PARERR &&
          disk_ioctl(0, GET_SECTOR_SIZE, nullptr) == RES_PARERR &&
          disk_ioctl(0, GET_BLOCK_SIZE, nullptr) == RES_PARERR &&
          disk_ioctl(0, 255, &block_size) == RES_PARERR && requests.empty(),
          "read-only ioctl handles sync, block size, null outputs and unsupported requests locally");

    // Each distinct initialization command must fail closed. CMD55 has two
    // roles: operating-condition negotiation and configuring the selected card.
    for (const Request request : {Request{55, 0}, Request{41, SDIO_CARD_OCR_MODE},
                                 Request{2, 0}, Request{3, 0}, Request{7, kAddress},
                                 Request{55, kAddress}, Request{6, 2}, Request{16, 512}})
    {
        fresh();
        failed_command = request.command;
        failed_command_argument = request.argument;
        rejectedInitialization();
        check(saw(request.command, request.argument), "initialization reaches the injected command failure");
    }
    for (const Request request : {Request{7, kAddress}, Request{55, kAddress},
                                 Request{6, 2}, Request{16, 512}})
    {
        fresh();
        response_command = request.command;
        response_argument = request.argument;
        response_value = kCardError;
        rejectedInitialization();
    }
    fresh();
    response_command = 8;
    response_value = 0x1ab;
    rejectedInitialization();
    check(!saw(55, 0) && !rp2350_sd_card.present(),
          "wrong CMD8 echo rejects initialization and quiet insertion discovery");
    fresh();
    response_command = 3;
    response_value = 0;
    rejectedInitialization();
    check(!saw(9, 0), "zero relative address is rejected before requesting geometry");
    fresh(false);
    setBits(83, 80, 0);
    setBits(73, 62, 0);
    setBits(49, 47, 0);
    rejectedInitialization();
    fresh(false);
    setBits(83, 80, 12);
    rejectedInitialization();
    fresh(false);
    high_capacity = true;
    rejectedInitialization();

    startCard();
    receive_busy_polls = 3;
    check(disk_read(0, data.data(), 10, 2) == RES_OK && receive_busy_polls == 0 &&
          !read_active, "busy receive polls complete before the multiblock read is stopped");
    for (const int command : {17, 18})
    {
        startCard();
        failed_command = command;
        check(disk_read(0, data.data(), 10, command == 17 ? 1 : 2) == RES_ERROR &&
              read_starts.empty() && stops > 0 && !read_active,
              "failed read command starts no DMA and still cleans up");
    }
    startCard();
    receive_start_error = true;
    check(disk_read(0, data.data(), 10, 2) == RES_ERROR && read_starts.empty() &&
          saw(12, 0) && !read_active, "failed receive setup cancels the pending multiblock read");
    startCard();
    receive_error = true;
    data.fill(0xa5);
    check(disk_read(0, data.data() + 1, 10, 2) == RES_ERROR &&
          read_starts == std::vector<uint32_t>({10}) && data[1] == 0xa5 && data[513] == 0xa5,
          "failed unaligned receive neither copies bounce data nor attempts the next sector");
    for (const unsigned count : {1U, 2U})
    {
        startCard();
        stop_error = true;
        check(disk_read(0, data.data(), 10, count) == RES_ERROR,
              "successful receive cannot hide driver cleanup failure");
    }
    startCard();
    failed_command = 12;
    check(disk_read(0, data.data(), 10, 2) == RES_ERROR && saw(12, 0),
          "failed multiblock stop command cannot acknowledge a read");
    startCard();
    response_command = 12;
    response_value = kCardError;
    check(disk_read(0, data.data(), 10, 2) == RES_ERROR && !read_active,
          "card-reported multiblock cancellation error cannot acknowledge a read");

    // Presence requires all three facts: no card error, READY_FOR_DATA and TRAN.
    for (const uint32_t status : {kReadyTran | kCardError, 4U << 9, (1U << 8) | (5U << 9)})
    {
        startCard();
        response_command = 13;
        response_value = status;
        check(!rp2350_sd_card.present() && !rp2350_sd_card.ready() &&
              rp2350_sd_card.sectorCount() == 0 && disk_status(0) == STA_NOINIT,
              "unhealthy/non-transfer status invalidates presence and cached geometry");
        check(disk_read(0, data.data(), 0, 1) == RES_NOTRDY,
              "invalidated media cannot expose stale sectors");
    }
    startCard();
    writes_allowed = true;
    transmit_error = true;
    response_command = 12;
    response_value = kCardError;
    check(!rp2350_sd_card.write(10, data.data(), 2) &&
          write_starts == std::vector<uint32_t>({10}) && saw(12, 0),
          "card-reported write cancellation error stops later sectors without retry");
    rp2350_sd_card.end();
    std::puts("RP2350 initialization, read cleanup and diskio failure scenarios passed");
}
