cmake_minimum_required(VERSION 3.20)

option(TORII_BUILD_FIRMWARE "Build the XIAO RP2040 firmware" OFF)

if(TORII_BUILD_FIRMWARE)
    if(NOT PICO_SDK_PATH AND DEFINED ENV{PICO_SDK_PATH})
        set(PICO_SDK_PATH "$ENV{PICO_SDK_PATH}")
    endif()
    if(NOT PICO_SDK_PATH)
        message(FATAL_ERROR
            "Set PICO_SDK_PATH to a Pico SDK checkout before building firmware")
    endif()
    include("${PICO_SDK_PATH}/external/pico_sdk_import.cmake")
endif()

project(torii_usb C CXX ASM)

include(CTest)
add_subdirectory(firmware)
add_subdirectory(host/c)
