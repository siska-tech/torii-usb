add_library(torii_protocol STATIC src/protocol.c)
target_include_directories(torii_protocol PUBLIC include)
target_compile_features(torii_protocol PUBLIC c_std_11)

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(torii_protocol PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

if(BUILD_TESTING AND NOT TORII_BUILD_FIRMWARE)
    add_executable(torii_protocol_test ../tests/protocol_test.c)
    target_link_libraries(torii_protocol_test PRIVATE torii_protocol)
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(torii_protocol_test PRIVATE -Wall -Wextra -Wpedantic -Werror)
    endif()
    add_test(NAME torii_protocol_test COMMAND torii_protocol_test)
endif()

if(TORII_BUILD_FIRMWARE)
    pico_sdk_init()

    include(FetchContent)
    if(PICO_PIO_USB_PATH)
        add_subdirectory("${PICO_PIO_USB_PATH}" "${CMAKE_BINARY_DIR}/pico-pio-usb")
    else()
        FetchContent_Declare(
            pico_pio_usb
            GIT_REPOSITORY https://github.com/sekigon-gonnoc/Pico-PIO-USB.git
            GIT_TAG b186e36bea0f3bcc2ee94ca1fa81e7f71281443a
            GIT_SHALLOW FALSE
        )
        FetchContent_MakeAvailable(pico_pio_usb)
    endif()

    add_executable(torii_firmware
        src/main.c
        src/uart_transport.c
        src/usb_host.c
        src/xpad_host.c
        src/usb_descriptors.c
        "${PICO_TINYUSB_PATH}/src/portable/raspberrypi/pio_usb/dcd_pio_usb.c"
        "${PICO_TINYUSB_PATH}/src/portable/raspberrypi/pio_usb/hcd_pio_usb.c"
    )
    target_include_directories(torii_firmware PRIVATE include src)
    target_compile_definitions(torii_firmware PRIVATE PIO_USB_USE_TINYUSB)
    target_link_libraries(torii_firmware PRIVATE
        torii_protocol
        pico_stdlib
        pico_multicore
        pico_pio_usb
        hardware_uart
        hardware_sync
        tinyusb_device
        tinyusb_host
    )
    target_compile_options(torii_firmware PRIVATE -Wall -Wextra)
    pico_enable_stdio_usb(torii_firmware 0)
    pico_enable_stdio_uart(torii_firmware 0)
    pico_add_extra_outputs(torii_firmware)
endif()
