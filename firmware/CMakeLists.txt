cmake_minimum_required(VERSION 3.13)

# Switch to pico2_w for the RP2350 production target.
if(NOT DEFINED PICO_BOARD)
    set(PICO_BOARD "pico_w")
endif()

set(BLUEPAD32_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/lib/bluepad32)
set(BTSTACK_ROOT ${PICO_SDK_PATH}/lib/btstack)

include(pico_sdk_import.cmake)

project(bridge C CXX ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

add_executable(bridge
    src/main.c
    src/bt_host.c
    src/bt_bonds.c
    src/log.c
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
    ${BLUEPAD32_ROOT}/src/components/bluepad32/include
)

# bluepad32 + pico_btstack include layout (mirrors examples/pico_w).
include_directories(
    ${CMAKE_CURRENT_LIST_DIR}/include
    ${BTSTACK_ROOT}/3rd-party/bluedroid/encoder/include
    ${BTSTACK_ROOT}/3rd-party/bluedroid/decoder/include
    ${BTSTACK_ROOT}/src
)

target_link_libraries(bridge
    pico_stdlib
    pico_cyw43_arch_none
    pico_btstack_classic
    pico_btstack_cyw43
    bluepad32
    tinyusb_device
    tinyusb_board
)

add_subdirectory(${BLUEPAD32_ROOT}/src/components/bluepad32 libbluepad32)

# TinyUSB owns the USB bus — disable pico-sdk's default stdio routes.
pico_enable_stdio_usb(bridge 0)
pico_enable_stdio_uart(bridge 0)

pico_add_extra_outputs(bridge)
