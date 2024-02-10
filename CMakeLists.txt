cmake_minimum_required(VERSION 3.13)

set(PICO_SDK_PATH "${CMAKE_CURRENT_LIST_DIR}/pico-sdk")
set(PICO_PIO_USB_PATH "${CMAKE_CURRENT_LIST_DIR}/Pico-PIO-USB")

include(pico_sdk_import.cmake)

project(gamepad)

pico_sdk_init()

add_subdirectory(${PICO_PIO_USB_PATH} pico_pio_usb)

add_compile_options(-Wall)

add_executable(gamepad src/gamepad.c)

target_link_libraries(gamepad
    pico_stdlib
    pico_multicore
    pico_pio_usb
)

pico_set_binary_type(gamepad copy_to_ram)
pico_add_extra_outputs(gamepad)
