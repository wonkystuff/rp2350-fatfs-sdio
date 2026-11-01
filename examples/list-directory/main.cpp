// SPDX-License-Identifier: MIT
// Copyright (c) 2026 wonkystuff
#include "rp2350_sd_card.hpp"
#include "ff.h"
#include "pico/stdlib.h"
#include <cstdio>

int main()
{
    stdio_init_all();
    sleep_ms(1500);
    rp2350_sd_card.prepareBus();

    FATFS filesystem{};
    const auto result = f_mount(&filesystem, "0:", 1);
    std::printf("Mount result: %u\n", static_cast<unsigned>(result));
    if (result == FR_OK)
    {
        DIR directory{};
        if (f_opendir(&directory, "0:/") == FR_OK)
        {
            FILINFO entry{};
            // Keep the example's output small; this is not a file browser.
            for (unsigned i = 0; i < 16; ++i)
            {
                const auto read_result = f_readdir(&directory, &entry);
                if (read_result != FR_OK || entry.fname[0] == '\0') break;
                std::printf("%s\n", entry.fname);
            }
            f_closedir(&directory);
        }
    }
    f_mount(nullptr, "0:", 0);
    rp2350_sd_card.end();
    std::puts("Done (read-only). Reset to run again.");
    while (true) tight_loop_contents();
}
