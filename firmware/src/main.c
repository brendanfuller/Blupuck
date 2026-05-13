#include "pico/stdlib.h"

int main(void) {
    stdio_init_all();

    // TODO: cyw43_arch_init() with WiFi disabled
    // TODO: bt_host_init() — Bluepad32 setup
    // TODO: bt_bonds_load()
    // TODO: usb_init() — TinyUSB device stack
    // TODO: wake_pulse() — boot-time mouse jiggle

    for (;;) {
        // TODO: tud_task(); bt_host_task();
        tight_loop_contents();
    }
}
