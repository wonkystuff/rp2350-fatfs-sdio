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
