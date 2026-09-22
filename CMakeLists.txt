cmake_minimum_required(VERSION 3.13)

add_compile_definitions(PICO_XOSC_STARTUP_DELAY_MULTIPLIER=64)

set(PICO_SDK_PATH "${CMAKE_CURRENT_LIST_DIR}/pico-sdk")
set(PICO_TINYUSB_PATH "${CMAKE_CURRENT_LIST_DIR}/tinyusb")

include(pico_sdk_import.cmake)

project(gamepad)

pico_sdk_init()

add_compile_options(-Wall)

add_executable(gamepad src/gamepad.c)
target_include_directories(gamepad PRIVATE src)
pico_generate_pio_header(gamepad ${CMAKE_CURRENT_LIST_DIR}/src/gpio_sample.pio)
target_link_libraries(gamepad pico_stdlib hardware_pio hardware_dma tinyusb_device tinyusb_board)
pico_add_extra_outputs(gamepad)
