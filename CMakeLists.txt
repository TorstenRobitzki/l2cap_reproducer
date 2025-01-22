#
# Copyright (c) 2022 Nordic Semiconductor
#
# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
#
cmake_minimum_required(VERSION 3.20.0)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(APPLICATION_CONFIG_DIR "${CMAKE_CURRENT_SOURCE_DIR}")

# add_child_image_from_source() in multi_image.cmake creates sub processes running
# cmake to build other images, which might fail to find the Zephyr package if this
# environment variable is not set.
if(NOT DEFINED ENV{ZEPHYR_BASE})
    set(ENV{ZEPHYR_BASE} ${CMAKE_CURRENT_LIST_DIR}/../zephyr)
endif()

if(NOT DEFINED ENV{ZEPHYR_NRF_MODULE_DIR})
    set(ENV{ZEPHYR_NRF_MODULE_DIR} ${CMAKE_CURRENT_LIST_DIR}/../nrf)
    set(ZEPHYR_NRF_MODULE_DIR ${CMAKE_CURRENT_LIST_DIR}/../nrf)
endif()

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(peripheral_cgms)

target_sources(app PRIVATE
  src/main.c
)
