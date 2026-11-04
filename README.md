# RP2350 FatFs SDIO

A small FatFs/SDIO integration for the Raspberry Pi Pico SDK, using
Rabbit Hole Computing's explicitly MIT-licensed RP2350 SDIO driver. The code
basically provides the _glue_ between these two external libraries:

- https://github.com/abbrev/fatfs
- https://github.com/rabbitholecomputing/SDIO_RP2350

The integration code is MIT licensed. FatFs retains its own permissive licence;
all component notices and pinned source revisions are in [NOTICE.md](NOTICE.md).
Release history and pending changes are recorded in the [changelog](#changelog).
This is not a fork of the historical combined no-OS SD library, and makes no
claim about the licensing of that project's code or earlier firmware releases.

The repository exists because of a dissatisfaction with the (otherwise brilliant)
[no-OS-FatFS-SD-SDIO-SPI-RPi-Pico](https://github.com/carlk3/no-OS-FatFS-SD-SDIO-SPI-RPi-Pico)
which refers to the ZuluSCSI codebase, the GPL-3.0 license being incompatible with
commercial/closed-source firmware. As such, it is likely that changes will be made here
as and when **wonkystuff®** firmware requires it!

## Automated Checks

The CI run summary includes line, function and branch coverage for `src/` and
`include/`, measured by a separate gcovr build of the native protocol and raw-write
tests. Test doubles, tests themselves and upstream dependencies are excluded from
the percentages. This measures host execution, not PIO/DMA or hardware coverage;
the real FatFs parser tests run separately in the sanitizer job.

On GitHub, open **Actions → CI → a run** to see the summary. Download the
`coverage-report` artifact, unzip it and open `index.html` for annotated source
showing uncovered lines and branches. Reports are retained for 14 days. Coverage
is informational initially, with no minimum percentage enforced.

GitHub Actions runs the host protocol, raw-write and real FatFs tests with
AddressSanitizer/UndefinedBehaviorSanitizer, compiles the Pico 2 example against
SDK 2.3.0, and checks the integration with selected clang-tidy rules. CodeQL
runs on pull requests, main-branch pushes and a weekly schedule. Workflow YAML
is checked with actionlint; Dependabot proposes weekly GitHub Actions updates.
The two CMake FetchContent revisions remain manually reviewed pins.

The licence workflow runs `reuse lint` and produces a downloadable ScanCode
inventory covering this repository and both exact FetchContent dependencies.
The inventory is a review report, not an automatic licence-compatibility verdict;
it does not cover a consuming application's SDK, runtime or other dependencies.
Third-party notice copies live in `third-party-notices/`; REUSE licence texts
live in `LICENSES/`. Existing third-party source attributions remain in place.

For local checks:

```sh
python3 -m pip install reuse==5.1.1
reuse lint
cmake -S tests -B build-tidy -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
clang-tidy -p build-tidy src/rp2350_sd_card.cpp src/rp2350_diskio.cpp
```

Repository administrators should separately verify secret scanning/push
protection in GitHub settings and select required checks in branch rules after
the first successful CI run. Those settings are not controlled by workflow YAML.

## AI-Assisted Development

OpenAI Codex/ChatGPT was used to help construct this library, including writing
and refactoring integration code, tests and documentation. This is disclosed so
that users can make an informed decision about using AI-assisted code.

The upstream FatFs and SDIO sources are separate third-party projects; this
disclosure concerns work in this integration repository, not their development.
AI assistance is not a guarantee of correctness or source provenance. The
maintainer remains responsible for changes accepted here; component attribution
and licence information are recorded separately in [NOTICE.md](NOTICE.md).
The tests and hardware checks described below do not establish universal card
compatibility or protection against data loss.

## Scope

Version 0.2.0 adds raw `write()` and `sync()` for an exclusive block
owner such as USB MSC. FatFs itself remains read-only. Writes use individually
completed CMD24 sectors, with no streaming state or deferred acknowledgement.
Failed writes may have partially changed media; there is no automatic retry or
power-loss guarantee. Use a backed-up test card for hardware acceptance.
These additions are not part of the fixed read-only v0.1.0 release.

Basic USB reads/writes and host eject followed by a primary-button restart
have been confirmed in the consuming application. File-hash, power-loss and
broader host/card compatibility testing remain separate acceptance work.

- RP2350, four-bit SDIO, 25 MHz standard-speed data transfers.
- FAT/exFAT via FatFs R0.16, including July 2026 metadata fixes and compressed
  Unicode tables; one volume and 512-byte sectors.
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

Configurations supplied by applications must use `FFCONF_DEF 80386` for this
FatFs revision. This integration keeps relative paths disabled, defines the
inactive `FF_PATH_DEPTH` default as 10, and leaves `FF_FS_CRTIME` disabled.
Do not substitute upstream's default configuration: it enables filesystem
writes and differs from this library's supported contract.

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

### Exclusive Raw Block Access

An owner such as USB MSC can call `begin()`, `read()`, `write()` and `sync()`
directly, without mounting FatFs. Do not expose raw storage to a host while a
local filesystem is mounted; serialization of individual calls alone cannot
make two independent filesystem owners safe.

`write()` waits for each sector's transfer and programming completion before
advancing. It stops at the first error and never automatically retries. Earlier
sectors may already be committed and the failing sector may have changed too:
the request is not atomic. The caller's source buffer must remain valid and
unchanged until the call returns, including when DMA reads it directly.

`sync()` checks card readiness; there is no MCU write-back cache. A later
successful sync does not repair or retroactively validate a failed write, and
cannot flush files cached by a USB host. `ready()` reports cached initialization
state, not a live health check or a sticky record of I/O failures. Applications
must handle failures explicitly. For USB MSC, revoke further host data access
after an error and require a fresh ownership session/card initialization before
resuming, rather than silently accepting writes to a reinserted card.

Transmit polling uses the pinned low-level driver's transfer timeout. The
adapter separately bounds its wait for READY_FOR_DATA in TRAN state. These are
per-stage limits, not a fixed maximum latency for a multi-sector request.

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
Write coverage includes aligned/unaligned source data, SDSC addressing, busy
polling, first/later-sector failures, failed stop/cancel, driver transmit timeout,
programming timeout and card-reported completion errors. Captured transmit
buffers represent attempted transfers, not a model of durable card contents.
They do not simulate PIO, DMA or a filesystem, nor establish throughput, WCET
or universal SD-card compatibility. Applications should test their additional
filesystem/session ownership and hotplug policy separately.

Additional failure scenarios cover initialization command/response rejection,
invalid geometry, direct-read and diskio argument validation, receive polling
and setup failures, multiblock cleanup errors, unaligned-read copy suppression,
presence-status invalidation and card-reported write cancellation errors.

To also test the real FatFs parser, pass an existing checkout of the exact
FatFs revision pinned in CMakeLists.txt (no download is performed by the tests):

```sh
cmake -S tests -B build-tests -DFATFS_SOURCE_DIR=/path/to/fatfs
cmake --build build-tests
ctest --test-dir build-tests --output-on-failure
```

This adds synthetic FAT16, FAT32 and exFAT sector fixtures covering long names,
directory termination, reads/seeks across boundaries, set-style text, missing
files and sector errors, plus rejection of excessive FAT sizes and invalid
exFAT cluster counts. It does not model an SD card's physical durability or
replace tests with host-formatted cards. The patched dependency and optional
filesystem tests pass normally and under AddressSanitizer/UBSan. The user also
confirmed that the consuming player firmware works with this upgrade on
hardware. This is functional acceptance, not a new throughput measurement,
long-duration integrity check or USB-storage qualification.

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

## Changelog

Record changes under Unreleased. When publishing, replace that heading with
the version and release date (YYYY-MM-DD), then start a new Unreleased section.
Keep published tags immutable.

### Unreleased

No changes yet.

### 0.2.0 - 2026-11-02

- Added synchronous raw-sector `write()` and `sync()` for an exclusive block
  owner such as USB MSC. Each sector completes before success is returned;
  failed writes are reported without automatic retries.
- Updated FatFs from R0.15 patch 1 to R0.16 at
  `ab992fcf2926b8a1ee3f0a8f9cefb6584174100d`, including metadata-validation
  fixes and compressed Unicode tables. Updated component notices.
- Application-provided `ffconf.h` now requires `FFCONF_DEF 80386`; documented
  relative-path depth and creation-time configuration defaults.
- Added raw-write/failure-handling tests and real FatFs parser tests over
  synthetic FAT16, FAT32 and exFAT media.
- Added disclosure of Codex/ChatGPT assistance in this integration's development.

FatFs file access remains read-only, and the upstream SDIO driver is unchanged.
Raw-sector writes do not provide USB transport, concurrent filesystem access
or power-loss protection.

### 0.1.0 - 2026-11-01

- Initial read-only RP2350 integration of FatFs R0.15 patch 1 and Rabbit Hole
  Computing's MIT-licensed SDIO_RP2350 driver, with pinned dependencies and
  retained component notices.
- Four-bit, 25 MHz card initialization, sector reads, geometry/status queries
  and FatFs disk adapter, with fixed-buffer support for unaligned reads.
- Pico SDK/CMake integration, example configuration and host protocol tests.

Extracted for wonkystuff development to clarify storage-code licensing and
provenance; this does not settle the licensing of the historical combined
SD library or earlier firmware releases.
