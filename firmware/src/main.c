#include <btstack_run_loop.h>
#include <pico/cyw43_arch.h>
#include <pico/stdlib.h>
#include <uni.h>

#include "bt_host.h"
#include "log.h"
#include "usb_cdc.h"
#include "usb_webusb.h"
#include "usb_xinput.h"
#include "wake.h"

// btstack owns the run loop; the USB stack is ticked from a repeating timer.
// Keeping tinyusb headers out of main.c avoids the btstack-vs-tinyusb HID
// symbol collision.
static btstack_timer_source_t usb_tick;

static void on_text_command(const char* cmd);

static void usb_tick_handler(btstack_timer_source_t* ts) {
    usb_xinput_tick();
    usb_cdc_tick();
    usb_webusb_tick();
    wake_tick();
    btstack_run_loop_set_timer(ts, 1);
    btstack_run_loop_add_timer(ts);
}

static void on_text_command(const char* cmd) {
    bt_host_handle_text_command(cmd);
}

int main(void) {
    if (cyw43_arch_init()) {
        return -1;
    }

    // LED ON = booted, init in progress. on_init_complete turns it OFF.
    cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 1);

    usb_xinput_init();
    usb_cdc_init(on_text_command);
    usb_webusb_init();

    uni_platform_set_custom(bt_host_get_platform());
    uni_init(0, NULL);

    usb_tick.process = usb_tick_handler;
    btstack_run_loop_set_timer(&usb_tick, 1);
    btstack_run_loop_add_timer(&usb_tick);

    btstack_run_loop_execute();
    return 0;
}
