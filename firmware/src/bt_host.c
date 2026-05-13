#include "bt_host.h"

#include <pico/cyw43_arch.h>
#include <uni.h>

#include "gamepad_state.h"
#include "translate.h"
#include "usb_hid.h"

static void on_init(int argc, const char** argv) {
    (void)argc;
    (void)argv;
}

static void on_init_complete(void) {
    uni_bt_start_scanning_and_autoconnect_unsafe();
    cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 0);
}

static uni_error_t on_device_discovered(bd_addr_t addr, const char* name, uint16_t cod, uint8_t rssi) {
    (void)addr; (void)name; (void)cod; (void)rssi;
    return UNI_ERROR_SUCCESS;
}

static void on_device_connected(uni_hid_device_t* d) { (void)d; }
static void on_device_disconnected(uni_hid_device_t* d) { (void)d; }

static uni_error_t on_device_ready(uni_hid_device_t* d) {
    // Light a single player LED matching the device's bluepad32 slot.
    // P1 = LED1, P2 = LED2, ... matching the Switch convention.
    if (d->report_parser.set_player_leds != NULL) {
        const int idx = uni_hid_device_get_idx_for_instance(d);
        d->report_parser.set_player_leds(d, (uint8_t)(1u << (idx & 0x03)));
    }
    return UNI_ERROR_SUCCESS;
}

static void on_controller_data(uni_hid_device_t* d, uni_controller_t* ctl) {
    if (ctl->klass != UNI_CONTROLLER_CLASS_GAMEPAD) {
        return;
    }
    if (uni_hid_device_get_idx_for_instance(d) != 0) {
        return;
    }

    bridge_gp_state_t state;
    translate_gamepad(&ctl->gamepad, &state);
    usb_hid_set_gamepad(0, &state);
}

static const uni_property_t* get_property(uni_property_idx_t idx) {
    (void)idx;
    return NULL;
}

static void on_oob_event(uni_platform_oob_event_t event, void* data) {
    (void)event;
    (void)data;
}

struct uni_platform* bt_host_get_platform(void) {
    static struct uni_platform plat = {
        .name = "bridge",
        .init = on_init,
        .on_init_complete = on_init_complete,
        .on_device_discovered = on_device_discovered,
        .on_device_connected = on_device_connected,
        .on_device_disconnected = on_device_disconnected,
        .on_device_ready = on_device_ready,
        .on_oob_event = on_oob_event,
        .on_controller_data = on_controller_data,
        .get_property = get_property,
    };
    return &plat;
}
