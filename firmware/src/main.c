#include <btstack_run_loop.h>
#include <pico/cyw43_arch.h>
#include <pico/stdlib.h>
#include <uni.h>

#include "bt_host.h"
#include "usb_hid.h"

// btstack owns the run loop; the USB stack is ticked from a repeating timer.
// Keeping tinyusb headers out of main.c avoids the btstack-vs-tinyusb HID
// symbol collision (HID_USAGE_PAGE_* is a macro on one side and an enum
// member on the other).
static btstack_timer_source_t usb_tick;

static void usb_tick_handler(btstack_timer_source_t* ts) {
    usb_hid_tick();
    btstack_run_loop_set_timer(ts, 1);
    btstack_run_loop_add_timer(ts);
}

int main(void) {
    if (cyw43_arch_init()) {
        return -1;
    }

    usb_hid_init();

    uni_platform_set_custom(bt_host_get_platform());
    uni_init(0, NULL);

    usb_tick.process = usb_tick_handler;
    btstack_run_loop_set_timer(&usb_tick, 1);
    btstack_run_loop_add_timer(&usb_tick);

    btstack_run_loop_execute();
    return 0;
}
