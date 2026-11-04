// SPDX-License-Identifier: MIT
/*
 * Card command sequencing adapted from SDIO_RP2350's MIT-licensed
 * sdfat_sdcard_rp2350.cpp at 0c757c2b3554e744b8821719607830753cfde262.
 * Modified for FatFs integration and synchronous raw block I/O: no SdFat, prefetch,
 * write checks or speed negotiation, and one completed transaction per read.
 *
 * Copyright (c) 2022-2025 Rabbit Hole Computing
 * Copyright (c) 2011-2025 Bill Greiman
 * Copyright (c) 2026 wonkystuff
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include "rp2350_sd_card.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include "hardware/gpio.h"
#include "hardware/irq.h"
#include "pico/time.h"
#include "sdio_rp2350.h"

Rp2350SdCard rp2350_sd_card;

namespace
{
constexpr uint32_t kSectorSize = 512;
constexpr uint32_t kCardErrors = 0xfff98008U;

// Decode only successful replies. Upstream's u32 convenience wrapper reads an
// uninitialised temporary when the underlying command rejects a response.
sdio_status_t commandResponse(uint8_t index, uint32_t argument, uint32_t &response, uint32_t flags)
{
    uint8_t bytes[4] = {};
    const auto status = rp2350_sdio_command(index, argument, bytes, sizeof(bytes), flags);
    response = 0;
    if (status == SDIO_OK)
        for (uint8_t byte : bytes) response = (response << 8) | byte;
    return status;
}

// CSD is a network-order 128-bit register. SD specification bit numbers count
// from the least significant end, unlike indices in the response byte array.
uint32_t csdBits(const uint8_t *csd, unsigned high, unsigned low)
{
    uint32_t value = 0;
    for (unsigned bit = high + 1; bit-- > low;)
        value = (value << 1) | ((csd[(127 - bit) / 8] >> (bit % 8)) & 1U);
    return value;
}
}

void Rp2350SdCard::prepareBus()
{
    // The driver assigns PIO functions but leaves pad pulls to the board.
    // CMD/DAT must idle high while neither host nor card is driving them;
    // explicitly replace reset pull-downs, including for an empty socket.
    gpio_disable_pulls(SDIO_CLK);
    for (unsigned pin : {SDIO_CMD, SDIO_D0, SDIO_D1, SDIO_D2, SDIO_D3})
        gpio_pull_up(pin);
    rp2350_sdio_init(rp2350_sdio_get_timing(SDIO_INITIALIZE));
    irq_set_enabled(SDIO_DMAIRQ, false);
}

bool Rp2350SdCard::command(uint8_t index, uint32_t argument, uint32_t &response, uint32_t flags)
{
    return commandResponse(index, argument, response, flags) == SDIO_OK;
}

bool Rp2350SdCard::begin()
{
    ready_ = false;
    sector_count_ = 0;
    relative_address_ = 0;
    rp2350_sdio_init(rp2350_sdio_get_timing(SDIO_INITIALIZE));
    sleep_us(1000);

    uint32_t response = 0;
    // Mount failures need one useful report, not a trace of every successful
    // command. These diagnostics are local to initialisation.
    sdio_status_t status = SDIO_OK;
    unsigned last_command = 0;
    const auto send = [&](uint8_t index, uint32_t argument, uint32_t flags = 0) {
        last_command = index;
        response = 0;
        status = commandResponse(index, argument, response, flags);
        return status == SDIO_OK;
    };
    const auto fail = [&](const char *stage) {
        std::printf("SDIO init failed: %s CMD%u status=%u response=0x%08lx\n",
                    stage, last_command, static_cast<unsigned>(status),
                    static_cast<unsigned long>(response));
        return false;
    };
    bool version_two = false;
    for (unsigned attempt = 0; attempt < 5; ++attempt)
    {
        rp2350_sdio_command(0, 0, nullptr, 0, SDIO_FLAG_NO_LOGMSG);
        sleep_us(1000);
        if (send(8, 0x1aa, SDIO_FLAG_NO_LOGMSG) && response == 0x1aa)
        {
            version_two = true;
            break;
        }
    }
    // Match the new upstream adapter's SD-v2 requirement. An absent or older
    // unsupported card must fail mounting, not become a zero-capacity volume.
    if (!version_two) return fail("interface condition");

    const uint32_t start = time_us_32();
    uint32_t ocr = 0;
    do
    {
        if (!send(55, 0) ||
            !send(41, SDIO_CARD_OCR_MODE, SDIO_FLAG_NO_CRC | SDIO_FLAG_NO_CMD_TAG))
            return fail("operating conditions");
        ocr = response;
        if (static_cast<uint32_t>(time_us_32() - start) >= SDIO_INIT_TIMEOUT_US)
            return fail("power-up timeout");
    } while (!(ocr & (1U << 31)));
    sector_addressing_ = (ocr & (1U << 30)) != 0;

    // Discover identity, assigned card address and geometry before selecting
    // the card. R2 responses contain the network-order 128-bit registers.
    uint8_t cid[16], csd[16];
    last_command = 2;
    response = 0;
    status = rp2350_sdio_command(2, 0, cid, sizeof(cid), SDIO_FLAG_NO_CRC | SDIO_FLAG_NO_CMD_TAG);
    if (status != SDIO_OK) return fail("card identity");
    if (!send(3, 0)) return fail("relative address");
    relative_address_ = response & 0xffff0000U;
    if (!relative_address_) return fail("invalid relative address");
    last_command = 9;
    response = 0;
    status = rp2350_sdio_command(9, relative_address_, csd, sizeof(csd), SDIO_FLAG_NO_CRC | SDIO_FLAG_NO_CMD_TAG);
    if (status != SDIO_OK) return fail("card geometry response");

    uint64_t sectors = 0;
    switch (csdBits(csd, 127, 126))
    {
    case 1: // SDHC/SDXC: C_SIZE counts 512 KiB units.
        sectors = (uint64_t{csdBits(csd, 69, 48)} + 1) * 1024;
        break;
    case 0: // SD-v2 SDSC cards still use byte addresses and the older CSD.
        sectors = ((uint64_t{csdBits(csd, 73, 62)} + 1) <<
                   (csdBits(csd, 49, 47) + 2 + csdBits(csd, 83, 80))) / kSectorSize;
        break;
    default: return fail("unsupported CSD structure");
    }
    if (!sectors || sectors > UINT32_MAX ||
        (csdBits(csd, 127, 126) == 1) != sector_addressing_ ||
        (!sector_addressing_ && sectors > (uint64_t{UINT32_MAX} + 1) / kSectorSize))
        return fail("invalid card capacity/addressing");

    // Select the card (CMD7), use four data lines (CMD55/ACMD6), then request
    // 512-byte blocks (CMD16). No write or speed-switch command is involved.
    if (!send(7, relative_address_) || (response & kCardErrors) ||
        !send(55, relative_address_) || (response & kCardErrors) ||
        !send(6, 2) || (response & kCardErrors) ||
        !send(16, kSectorSize) || (response & kCardErrors)) return fail("select/configure card");

    sector_count_ = static_cast<uint32_t>(sectors);
    rp2350_sdio_init(rp2350_sdio_get_timing(SDIO_STANDARD));
    ready_ = true;
    // Validate the data bus with a read only. Never rewrite sector zero.
    if (!read(0, sector_buffer_, 1))
    {
        ready_ = false;
        sector_count_ = 0;
        return fail("sector-zero data-bus check (see read error)");
    }
    return ready_;
}

bool Rp2350SdCard::finishRead(bool multiple)
{
    const bool stopped = rp2350_sdio_stop() == SDIO_OK;
    if (!multiple) return stopped;
    uint32_t response = 0;
    if (!command(12, 0, response) || (response & kCardErrors)) return false;
    return waitReady() && stopped;
}

bool Rp2350SdCard::waitReady()
{
    uint32_t response = 0;
    const uint32_t start = time_us_32();
    do
    {
        if (!command(13, relative_address_, response) || (response & kCardErrors)) return false;
        // R1 must report READY_FOR_DATA and CURRENT_STATE == TRAN.
        if ((response & (1U << 8)) && ((response >> 9) & 15U) == 4) return true;
    } while (static_cast<uint32_t>(time_us_32() - start) < SDIO_TRANSFER_TIMEOUT_US);
    return false;
}

bool Rp2350SdCard::readAligned(uint32_t sector, uint8_t *destination, uint32_t count)
{
    const bool multiple = count > 1;
    uint32_t response = 0;
    const uint32_t address = sector_addressing_ ? sector : sector * kSectorSize;
    const bool started = command(multiple ? 18 : 17, address, response, SDIO_FLAG_STOP_CLK) &&
                         !(response & kCardErrors) &&
                         rp2350_sdio_rx_start(destination, count, kSectorSize) == SDIO_OK;
    sdio_status_t result = SDIO_ERR_RESPONSE_TIMEOUT;
    if (started)
    {
        do { result = rp2350_sdio_rx_poll(nullptr); } while (result == SDIO_BUSY);
    }
    // Even a failed transfer must leave no ongoing read for a future seek or
    // presence check. Keep cleanup's result separate from the data outcome.
    const bool finished = finishRead(multiple);
    if (!started || result != SDIO_OK || !finished)
        std::printf("SDIO read failed: sector=%lu count=%lu started=%u status=%u response=0x%08lx cleanup=%u\n",
                    static_cast<unsigned long>(sector), static_cast<unsigned long>(count),
                    static_cast<unsigned>(started), static_cast<unsigned>(result),
                    static_cast<unsigned long>(response), static_cast<unsigned>(finished));
    return started && result == SDIO_OK && finished;
}

bool Rp2350SdCard::read(uint32_t sector, uint8_t *destination, uint32_t count)
{
    if (!ready_ || !destination || !count || sector >= sector_count_ || count > sector_count_ - sector)
        return false;
    if ((reinterpret_cast<uintptr_t>(destination) & 3U) != 0)
    {
        for (uint32_t index = 0; index < count; ++index)
        {
            if (!readAligned(sector + index, sector_buffer_, 1)) return false;
            std::memcpy(destination + index * kSectorSize, sector_buffer_, kSectorSize);
        }
        return true;
    }
    while (count)
    {
        const uint32_t blocks = std::min<uint32_t>(count, SDIO_MAX_BLOCKS_PER_REQ);
        if (!readAligned(sector, destination, blocks)) return false;
        sector += blocks;
        destination += blocks * kSectorSize;
        count -= blocks;
    }
    return true;
}

bool Rp2350SdCard::writeSector(uint32_t sector, const uint8_t *source)
{
    uint32_t response = 0;
    const uint32_t address = sector_addressing_ ? sector : sector * kSectorSize;
    const bool started = command(24, address, response, SDIO_FLAG_STOP_CLK) &&
                         !(response & kCardErrors) &&
                         rp2350_sdio_tx_start(source, 1, kSectorSize) == SDIO_OK;
    sdio_status_t result = SDIO_ERR_RESPONSE_TIMEOUT;
    if (started)
    {
        do { result = rp2350_sdio_tx_poll(nullptr); } while (result == SDIO_BUSY);
    }
    const bool stopped = rp2350_sdio_stop() == SDIO_OK;
    // Successful CMD24 ends itself. An incomplete/error transfer may leave
    // the card waiting for data, so cancel it before checking programming state.
    // Cancellation only runs after a failed transfer, which already rules out
    // success below. Attempt cleanup, but its result cannot rescue that write.
    if (!started || result != SDIO_OK)
        command(12, 0, response);
    const bool settled = waitReady();
    return started && result == SDIO_OK && stopped && settled;
}

bool Rp2350SdCard::write(uint32_t sector, const uint8_t *source, uint32_t count)
{
    if (!ready_ || !source || !count || sector >= sector_count_ || count > sector_count_ - sector)
        return false;
    // Start with individually completed CMD24 writes. Unlike multi-block
    // streaming, this has no retained transfer state or deferred acknowledgement.
    for (uint32_t index = 0; index < count; ++index, source += kSectorSize)
    {
        const uint8_t *aligned = source;
        if ((reinterpret_cast<uintptr_t>(source) & 3U) != 0)
        {
            std::memcpy(sector_buffer_, source, kSectorSize);
            aligned = sector_buffer_;
        }
        if (!writeSector(sector + index, aligned)) return false;
    }
    return true;
}

bool Rp2350SdCard::sync()
{
    // No MCU write-back cache exists. Verify the card has completed internal
    // programming and is back in TRAN before a caller acknowledges host eject.
    return ready_ && waitReady();
}

bool Rp2350SdCard::present()
{
    uint32_t response = 0;
    if (!ready_)
    {
        // With no card-detect switch, probe an idle/new card at startup speed.
        // The caller must space no-card polls; do not run full
        // initialization or read sector zero on every empty-socket poll.
        rp2350_sdio_init(rp2350_sdio_get_timing(SDIO_INITIALIZE));
        sleep_us(1000);
        rp2350_sdio_command(0, 0, nullptr, 0, SDIO_FLAG_NO_LOGMSG);
        sleep_us(1000);
        return command(8, 0x1aa, response, SDIO_FLAG_NO_LOGMSG) && response == 0x1aa;
    }

    if (command(13, relative_address_, response) && !(response & kCardErrors) &&
        (response & (1U << 8)) && ((response >> 9) & 15U) == 4) return true;

    // The next poll must discover a new card, not keep asking for the removed
    // card's assigned address. FatFs must also see an uninitialized disk.
    end();
    return false;
}

void Rp2350SdCard::end()
{
    if (ready_) rp2350_sdio_stop();
    ready_ = false;
    sector_count_ = 0;
    relative_address_ = 0;
    sector_addressing_ = false;
}
