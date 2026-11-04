# SPDX-License-Identifier: MIT
# Copyright (c) 2026 wonkystuff
cmake_minimum_required(VERSION 3.16)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(rp2350_fatfs_sdio_tests LANGUAGES CXX)
    include(CTest)
endif()

# No SDK or network needed: only the external hardware/driver boundary is fake.
add_library(rp2350_sdio_test_card STATIC
    ../src/rp2350_sd_card.cpp
    ../src/rp2350_diskio.cpp
    support/fake_sdio.cpp)
target_compile_features(rp2350_sdio_test_card PUBLIC cxx_std_17)
target_include_directories(rp2350_sdio_test_card PUBLIC
    support/sdio_stubs support ../include ../config)
add_executable(sd_card_protocol_tests card_tests.cpp)
target_link_libraries(sd_card_protocol_tests PRIVATE rp2350_sdio_test_card)
add_test(NAME sd_card_protocol_tests COMMAND sd_card_protocol_tests)
set_tests_properties(sd_card_protocol_tests PROPERTIES TIMEOUT 10)
add_executable(sd_card_write_tests write_tests.cpp)
target_link_libraries(sd_card_write_tests PRIVATE rp2350_sdio_test_card)
add_test(NAME sd_card_write_tests COMMAND sd_card_write_tests)
set_tests_properties(sd_card_write_tests PROPERTIES TIMEOUT 10)
add_executable(sd_card_failure_tests failure_tests.cpp)
target_link_libraries(sd_card_failure_tests PRIVATE rp2350_sdio_test_card)
add_test(NAME sd_card_failure_tests COMMAND sd_card_failure_tests)
set_tests_properties(sd_card_failure_tests PROPERTIES TIMEOUT 10)

# Optional real-filesystem coverage using an already downloaded dependency.
# Keep the default protocol tests independent of network access and the SDK.
set(FATFS_SOURCE_DIR "" CACHE PATH "FatFs checkout for filesystem regression tests")
if(FATFS_SOURCE_DIR)
    enable_language(C)
    foreach(file ff.c ff.h ffunicode.c diskio.h)
        configure_file("${FATFS_SOURCE_DIR}/source/${file}" "fatfs/${file}" COPYONLY)
    endforeach()
    configure_file(../config/ffconf.h fatfs/ffconf.h COPYONLY)
    add_executable(fatfs_filesystem_tests filesystem_tests.cpp
        "${CMAKE_CURRENT_BINARY_DIR}/fatfs/ff.c"
        "${CMAKE_CURRENT_BINARY_DIR}/fatfs/ffunicode.c")
    target_include_directories(fatfs_filesystem_tests PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/fatfs")
    target_compile_features(fatfs_filesystem_tests PRIVATE cxx_std_17)
    add_test(NAME fatfs_filesystem_tests COMMAND fatfs_filesystem_tests)
    set_tests_properties(fatfs_filesystem_tests PROPERTIES TIMEOUT 10)
endif()
