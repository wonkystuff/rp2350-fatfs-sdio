# RP2350 FatFs SDIO

A small, read-only FatFs integration for the Raspberry Pi Pico SDK, using
Rabbit Hole Computing's explicitly MIT-licensed RP2350 SDIO driver.

The integration code is MIT licensed. FatFs retains its own permissive licence;
all component notices and pinned source revisions are in [NOTICE.md](NOTICE.md).
This is not a fork of the historical combined no-OS SD library, and makes no
claim about the licensing of that project's code or earlier firmware releases.

## Scope

This branch experiments with raw `write()` and `sync()` for an exclusive block
owner such as USB MSC. FatFs itself remains read-only. Writes use individually
completed CMD24 sectors, with no streaming state or deferred acknowledgement.
Failed writes may have partially changed media; there is no automatic retry or
power-loss guarantee. Use a backed-up test card for hardware acceptance.
This experiment is not part of the fixed read-only v0.1.0 release.

Basic USB reads/writes and host eject followed by a primary-button restart
have been confirmed in the consuming application. File-hash, power-loss and
broader host/card compatibility testing remain separate acceptance work.

- RP2350, four-bit SDIO, 25 MHz standard-speed data transfers.
- FAT/exFAT via FatFs R0.15 patch 1; one volume and 512-byte sectors.
- SD-v2 initialization, including sector-addressed SDHC/SDXC and byte-addressed
  SD-v2 SDSC. Geometry is checked; sector counts must fit in 32 bits.
- Synchronous, allocation-free card protocol and disk adapter. Aligned reads
  use DMA directly; unaligned buffers use one fixed 512-byte bounce buffer.
- No SPI, RP2040, FatFs file writes, formatting, prefetch, RTOS support or speed negotiation.
- One physical card, one driver instance and one serialized caller. The exposed
  `rp2350_sd_card` is that instance; constructing more objects does not create
  independent PIO/DMA drivers.

Raw-sector writes are explicit operations for an exclusive owner; FatFs stays
read-only and initialization never runs upstream's write check. Read-only FatFs
is not a hardware write-protection mechanism.

## Use With CMake

Initialize the Pico SDK first, then:

```cmake
# Optional: provide BOTH ffconf.h and sdio_rp2350_config.h for your application.
# Otherwise the library's config/ example is used.
set(RP2350_FATFS_SDIO_CONFIG_DIR "${CMAKE_CURRENT_SOURCE_DIR}/sd-config")
add_subdirectory(path/to/rp2350-fatfs-sdio sd-card)
target_link_libraries(my_application PRIVATE rp2350_fatfs_sdio::rp2350_fatfs_sdio)
```

CMake 3.16+, C++17 and the Pico SDK are required. The current integration has
been compiled with SDK 2.3.0 for RP2350 Arm. A first firmware configure downloads
the two pinned Git dependencies; populated dependency checkouts can be rebuilt
offline. Standard FetchContent source overrides also work for offline builds.

The example configuration uses CLK GP2, CMD GP3 and DAT0..3 GP4..7, PIO1 SM0,
DMA channels 4/5 and DMA IRQ1. **Check wiring and resource conflicts before
running it.** Match `SDIO_GPIO_FUNC` to the selected PIO and keep DAT0..3
consecutive. Pins above 31 use the upstream driver's PIO GPIO-base support;
that wiring has not been hardware-verified here.

The FatFs configuration supports long filenames with a static work buffer,
not a heap allocation. Its code page is 932; choose the appropriate code page
for your filenames. Read-only, single-volume, 512-byte sectors, 32-bit LBA and
non-reentrant access are enforced at compile time. Changing these limitations
requires implementing and testing the corresponding driver support, not just
editing ffconf.h.

Only unchanged upstream sources are compiled. CMake stages their headers and
source files in the build directory so quoted includes select the application's
`ffconf.h` and the SDK-generated PIO header rather than upstream examples.

## Ownership And Card Lifecycle

```text
application -> FatFs -> diskio -> Rp2350SdCard -> MIT SDIO PIO/DMA driver -> card
```

1. Call `rp2350_sd_card.prepareBus()` once during startup, before other drivers
   claim DMA/PIO resources. It sets CMD/DAT pull-ups, disables CLK pulls, claims
   resources and leaves the selected DMA IRQ disabled on the calling core.
2. On the core that will own all card and filesystem access, call
   `f_mount(&filesystem, "0:", 1)`. FatFs invokes card initialization, which
   enables the DMA IRQ there. Single-core use works too. If another core was
   used for preparation, keep its SD IRQ disabled.
3. Use ordinary FatFs APIs. Each read, including error cleanup, finishes its
   transaction before returning. Do not call from an audio/timer interrupt or
   concurrently from another core. The library does not supply locks.
4. When appropriate, call `present()`. For initialized media it checks status;
   failure invalidates readiness and geometry. For unavailable media it uses
   quiet, low-speed CMD0/CMD8 discovery, not the previous card's address.
5. On removal, invalidate application handles and unregister the filesystem
   with `f_mount(nullptr, "0:", 0)`. Finding insertion does not initialize or
   mount the new card: mount it again before opening files.
6. On deliberate shutdown, close files, unregister FatFs and call `end()`.
   PIO/DMA resource reservations remain owned until reboot; `end()` is not a
   resource-release API.

Presence polling is application policy. Successful reads can establish that
a card remains present; failed reads can trigger a status check. **An empty
filesystem or idle application has no reads to reveal removal**, so keep a
slow periodic check in that case (for example, 50 ms). No background thread,
timer, hotplug task, or automatic mount-state owner is created by this library.

The diskio API intentionally makes `disk_status()` a cheap local readiness
query, not an electrical card-detect test. There is no card-detect switch input.
Failure diagnostics print only on failed initialization/reads, not successful
transactions; unavailable-card discovery is quiet. Schedule all blocking work
in the storage owner's context.

## Example

`examples/list-directory` mounts once, lists the root directory and unmounts.
It never creates or changes files. With `PICO_SDK_PATH` pointing to your SDK:

```sh
cmake -S examples/list-directory -B build-example -DPICO_BOARD=pico2
cmake --build build-example
```

Flash `build-example/sd_list.uf2` using your usual explicit flashing process.
The example uses USB serial and the wiring in `config/`; it is not a hotplug UI.

## Native Tests

No Pico SDK, network connection or hardware is needed:

```sh
cmake -S tests -B build-tests
cmake --build build-tests
ctest --test-dir build-tests --output-on-failure
```

Tests compile the real protocol/diskio sources with fake external driver,
GPIO, IRQ and time boundaries. They cover pin bias, geometry/addressing,
aligned/unaligned reads, burst splitting, cleanup, timeouts and card discovery.
They do not simulate PIO, DMA or a filesystem, nor establish throughput, WCET
or universal SD-card compatibility. Applications should test their additional
filesystem/session ownership and hotplug policy separately.

The integration before extraction was tested in a four-stream embedded audio
application, including card reinsertion. Its reported 15-second four-stereo-
stream test at 400% speed had no overruns or read errors. That is an observation
on particular hardware/cards, not a library performance guarantee. The extracted
integration has also passed application hardware tests after extraction. The
directory-listing example has compilation coverage only, not separate hardware
acceptance. Neither report establishes compatibility with every board or card.

## Repository Boundary

### Contribution Licensing

Review the actual licence/provenance of every reused source file and dependency
at its pinned revision, not just a repository's top-level licence. Preserve
copyright notices and attribution, and record new dependencies/adaptations in
NOTICE.md with their terms. Do not remove licence metadata to make an
implementation appear permissive. Do not import code with unresolved or
incompatible terms; clarify permission before incorporating it. This includes
code proposed by AI tools: generated code is not a provenance guarantee.

Everything required to build/test this library is within this directory or its
explicitly pinned public dependencies. The application that first used it is
not part of this package and need not be open-sourced to use the library.
Preserve the notices in LICENSE, LICENSES/ and source headers when distributing
this code; review other application/SDK/toolchain dependencies independently.
