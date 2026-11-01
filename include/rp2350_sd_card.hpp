// SPDX-License-Identifier: MIT
// Copyright (c) 2026 wonkystuff
#pragma once

#include <cstdint>

// One physical card. After startup-only bus preparation, all commands and
// filesystem access belong to one caller context. No internal locking.
class Rp2350SdCard
{
public:
    // Reserve configured DMA/PIO at startup, before other users claim them.
    // Leaves the configured IRQ disabled on the calling core; begin() enables
    // it on the core that will own card access (possibly this same core).
    void prepareBus();
    bool begin();
    void end();
    // Status check for initialized media; quiet CMD0/CMD8 discovery otherwise.
    // Detecting removal invalidates readiness; finding insertion does not mount.
    bool present();
    bool read(uint32_t sector, uint8_t *destination, uint32_t count);
    // Raw block writes for an exclusive owner (for example USB MSC). FatFs
    // remains read-only. Success includes card programming completion; failure
    // may leave partially written media and must not be retried blindly.
    bool write(uint32_t sector, const uint8_t *source, uint32_t count);
    bool sync();
    uint32_t sectorCount() const { return sector_count_; }
    bool ready() const { return ready_; }

private:
    bool command(uint8_t index, uint32_t argument, uint32_t &response, uint32_t flags = 0);
    bool finishRead(bool multiple);
    bool waitReady();
    bool writeSector(uint32_t sector, const uint8_t *source);
    bool readAligned(uint32_t sector, uint8_t *destination, uint32_t count);

    // Card-discovered geometry/addressing, valid only after a successful begin.
    uint32_t sector_count_{0};
    uint32_t relative_address_{0};
    bool sector_addressing_{false};
    bool ready_{false};
    // Mechanical bounce buffer for FatFs byte-aligned destinations, not
    // prefetch or retained PCM history. Aligned bulk reads avoid this copy.
    alignas(4) uint8_t sector_buffer_[512]{};
};

// One fixed-lifetime card shared by the FatFs disk adapter and application.
extern Rp2350SdCard rp2350_sd_card;
