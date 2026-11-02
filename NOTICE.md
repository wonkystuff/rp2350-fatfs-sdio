# Component Provenance And Notices

| Component | Exact source | Terms |
| --- | --- | --- |
| Integration, CMake, tests and example | This repository; Copyright (c) 2026 wonkystuff | MIT, see LICENSE |
| Low-level SDIO driver and PIO | https://github.com/rabbitholecomputing/SDIO_RP2350 at `0c757c2b3554e744b8821719607830753cfde262` | MIT, see LICENSES/SDIO_RP2350.txt and upstream headers |
| Adapted card command sequencing | Same revision, `src/sdfat_sdcard_rp2350.cpp` | MIT; Rabbit Hole Computing 2022-2025 and Bill Greiman 2011-2025 notices retained in `src/rp2350_sd_card.cpp` |
| FatFs | https://github.com/abbrev/fatfs at `f81eb28dec9746ba9f8623aaaa631dc96e60093a`, R0.15 patch 1 | ChaN's permissive licence, see LICENSES/FatFs.txt and source headers |

The build compiles upstream `src/sdio_rp2350.cpp` plus its generated PIO header,
and FatFs `source/ff.c` and `source/ffunicode.c`, using the corresponding public
headers. The upstream SdFat adapter is the credited reference for our adapted
protocol code, not a compiled dependency. SdFat itself is not linked.

No code is fetched or compiled from the historical combined
`no-OS-FatFS-SD-SDIO-SPI-RPi-Pico` repository. Its licence status is not resolved
or changed by this integration; this package is based on the explicitly
MIT-licensed RP2350 implementation instead. No GPL metadata directory has been
deleted as a substitute for changing the actual source dependency.

The Pico SDK and compiler runtime are external prerequisites, not relicensed
by this repository. The USB-serial example additionally uses SDK USB support
and TinyUSB. Preserve the applicable SDK/component notices when distributing
example firmware or your own application. This component list is not a complete
licence inventory for every possible application or toolchain build.

The raw write/sync additions use the same pinned MIT driver's
public transmit API and the SD command protocol. They introduce no additional
dependency and import no code from the historical combined SD library. Existing
card-adapter attribution and licence notices are retained.
