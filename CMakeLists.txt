# SPDX-License-Identifier: MIT
# Copyright (c) 2026 wonkystuff
cmake_minimum_required(VERSION 3.16)

if(NOT TARGET pico_stdlib OR NOT COMMAND pico_generate_pio_header)
    message(FATAL_ERROR "Initialize the Pico SDK before adding rp2350-fatfs-sdio")
endif()
if(NOT PICO_PLATFORM MATCHES "^rp2350")
    message(FATAL_ERROR "rp2350-fatfs-sdio currently supports RP2350 only")
endif()

include(FetchContent)
# Explicitly permissive sources only. Never substitute the historical combined
# SD library: removing licence metadata would not replace its implementation.
FetchContent_Declare(rp2350_fatfs_sdio_fatfs
    GIT_REPOSITORY https://github.com/abbrev/fatfs.git
    GIT_TAG f81eb28dec9746ba9f8623aaaa631dc96e60093a
)
FetchContent_Declare(rp2350_fatfs_sdio_driver
    GIT_REPOSITORY https://github.com/rabbitholecomputing/SDIO_RP2350.git
    GIT_TAG 0c757c2b3554e744b8821719607830753cfde262
)
FetchContent_MakeAvailable(rp2350_fatfs_sdio_fatfs rp2350_fatfs_sdio_driver)

# A consumer supplies its board/resource and filesystem configuration together.
# Stage those exact headers to avoid accidentally finding upstream examples.
if(NOT DEFINED RP2350_FATFS_SDIO_CONFIG_DIR)
    set(RP2350_FATFS_SDIO_CONFIG_DIR "${CMAKE_CURRENT_SOURCE_DIR}/config")
endif()
foreach(header ffconf.h sdio_rp2350_config.h)
    if(NOT EXISTS "${RP2350_FATFS_SDIO_CONFIG_DIR}/${header}")
        message(FATAL_ERROR "Missing ${RP2350_FATFS_SDIO_CONFIG_DIR}/${header}")
    endif()
    configure_file("${RP2350_FATFS_SDIO_CONFIG_DIR}/${header}"
        "${CMAKE_CURRENT_BINARY_DIR}/config/${header}" COPYONLY)
endforeach()

# FatFs's quoted ffconf.h would otherwise select its adjacent example config.
foreach(file ff.c ff.h ffunicode.c diskio.h)
    configure_file("${rp2350_fatfs_sdio_fatfs_SOURCE_DIR}/source/${file}"
        "${CMAKE_CURRENT_BINARY_DIR}/fatfs/${file}" COPYONLY)
endforeach()
# Likewise use this SDK's generated PIO header, not upstream's checked-in one.
configure_file("${rp2350_fatfs_sdio_driver_SOURCE_DIR}/src/sdio_rp2350.cpp"
    "${CMAKE_CURRENT_BINARY_DIR}/sdio-generated/sdio_rp2350.cpp" COPYONLY)

add_library(rp2350_fatfs_sdio STATIC
    "${CMAKE_CURRENT_BINARY_DIR}/fatfs/ff.c"
    "${CMAKE_CURRENT_BINARY_DIR}/fatfs/ffunicode.c"
    "${CMAKE_CURRENT_BINARY_DIR}/sdio-generated/sdio_rp2350.cpp"
    src/rp2350_sd_card.cpp
    src/rp2350_diskio.cpp
)
add_library(rp2350_fatfs_sdio::rp2350_fatfs_sdio ALIAS rp2350_fatfs_sdio)
target_compile_features(rp2350_fatfs_sdio PUBLIC cxx_std_17)
target_include_directories(rp2350_fatfs_sdio PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${CMAKE_CURRENT_BINARY_DIR}/config"
    "${CMAKE_CURRENT_BINARY_DIR}/fatfs"
)
target_include_directories(rp2350_fatfs_sdio PRIVATE
    "${CMAKE_CURRENT_BINARY_DIR}/sdio-generated"
    "${rp2350_fatfs_sdio_driver_SOURCE_DIR}/include"
)
target_link_libraries(rp2350_fatfs_sdio PUBLIC
    pico_stdlib hardware_pio hardware_dma hardware_clocks)
pico_generate_pio_header(rp2350_fatfs_sdio
    "${rp2350_fatfs_sdio_driver_SOURCE_DIR}/src/sdio_rp2350.pio"
    OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/sdio-generated")
