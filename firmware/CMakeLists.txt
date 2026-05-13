cmake_minimum_required(VERSION 3.13)

include(pico_sdk_import.cmake)

# Switch to pico2_w for the RP2350 production target.
set(PICO_BOARD pico_w CACHE STRING "Target board")

project(bt_controller_bridge C CXX ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

add_executable(bridge
    src/main.c
    src/bt_host.c
    src/bt_bonds.c
    src/usb_descriptors.c
    src/usb_hid.c
    src/usb_cdc.c
    src/usb_webusb.c
    src/translate.c
    src/mapping.c
    src/wake.c
)

target_include_directories(bridge PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/include
)

target_link_libraries(bridge
    pico_stdlib
    pico_cyw43_arch_none
    tinyusb_device
    tinyusb_board
    # bluepad32 — wired in once lib/bluepad32 submodule is added
)

# Stdio routed through our own composite CDC interface, not the SDK default.
pico_enable_stdio_usb(bridge 0)
pico_enable_stdio_uart(bridge 1)

pico_add_extra_outputs(bridge)
