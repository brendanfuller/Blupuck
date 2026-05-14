#include "bt_host.h"

#include <pico/cyw43_arch.h>
#include <string.h>
#include <uni.h>

#include "gamepad_state.h"
#include "log.h"
#include "translate.h"
#include "usb_xinput.h"
#include "wake.h"

static void on_init(int argc, const char** argv) {
    (void)argc;
    (void)argv;
}

static void on_init_complete(void) {
    log_line("bt: init complete, scanning");
    // Bonds in bluepad32's NVM survive across reboots; let them. The
    // `forget` CDC command clears them on demand.
    uni_bt_start_scanning_and_autoconnect_unsafe();
    cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 0);
}

void bt_host_handle_text_command(const char* cmd) {
    if (cmd[0] == 0) return;
    log_str("cmd: ");
    log_line(cmd);
    if (strcmp(cmd, "forget") == 0) {
        uni_bt_del_keys_unsafe();
        log_line("bt: keys cleared");
    } else if (strcmp(cmd, "help") == 0) {
        log_line("commands: forget, help");
    } else {
        log_str("unknown: ");
        log_line(cmd);
    }
}

static uni_error_t on_device_discovered(bd_addr_t addr, const char* name, uint16_t cod, uint8_t rssi) {
    (void)addr; (void)name; (void)cod; (void)rssi;
    return UNI_ERROR_SUCCESS;
}

// Per-slot mouse-mode state. Pressing the Switch Capture button toggles
// whether a controller drives the cursor (right stick → mouse deltas,
// A/B → mouse clicks) or the XInput slot.
static bool mouse_mode[BRIDGE_MAX_PLAYERS];
static uint32_t prev_buttons[BRIDGE_MAX_PLAYERS];

static int8_t stick_to_mouse_delta(int16_t v) {
    const int DEAD = 40;            // ignore stick drift around center
    if (v > -DEAD && v < DEAD) return 0;
    int32_t s = v / 32;             // bluepad32 ±512 → roughly ±15 px/frame
    if (s > 30)  s = 30;
    if (s < -30) s = -30;
    return (int8_t)s;
}

static void on_device_connected(uni_hid_device_t* d) {
    log_str("bt: connected idx=");
    log_u((uint32_t)uni_hid_device_get_idx_for_instance(d));
    log_line("");
}

static void on_device_disconnected(uni_hid_device_t* d) {
    const int idx = uni_hid_device_get_idx_for_instance(d);
    log_str("bt: disconnected idx=");
    log_u((uint32_t)idx);
    log_line("");
    if (idx < 0 || idx >= BRIDGE_MAX_PLAYERS) return;
    mouse_mode[idx] = false;
    prev_buttons[idx] = 0;
    usb_xinput_set_controller_present((uint8_t)idx, false);
}

static uni_error_t on_device_ready(uni_hid_device_t* d) {
    const int idx = uni_hid_device_get_idx_for_instance(d);
    log_str("bt: ready idx=");
    log_u((uint32_t)idx);
    log_line("");

    if (idx < 0 || idx >= BRIDGE_MAX_PLAYERS) return UNI_ERROR_SUCCESS;

    // Light a single player LED matching the controller's slot.
    if (d->report_parser.set_player_leds != NULL) {
        d->report_parser.set_player_leds(d, (uint8_t)(1u << idx));
    }
    usb_xinput_set_controller_present((uint8_t)idx, true);
    return UNI_ERROR_SUCCESS;
}

static void on_controller_data(uni_hid_device_t* d, uni_controller_t* ctl) {
    if (ctl->klass != UNI_CONTROLLER_CLASS_GAMEPAD) return;
    const int idx = uni_hid_device_get_idx_for_instance(d);
    if (idx < 0 || idx >= BRIDGE_MAX_PLAYERS) return;

    bridge_gp_state_t state;
    translate_gamepad(&ctl->gamepad, &state);

    // Capture button rising-edge toggles mouse mode for this slot.
    const uint32_t pressed = state.buttons & ~prev_buttons[idx];
    prev_buttons[idx] = state.buttons;
    if (pressed & BRIDGE_BTN_CAPTURE) {
        mouse_mode[idx] = !mouse_mode[idx];
        log_str("xin: slot ");
        log_u((uint32_t)idx);
        log_line(mouse_mode[idx] ? " mouse mode ON" : " mouse mode OFF");
        // Visual indicator: HOME-button ring LED. Player LEDs stay at the
        // single-slot indicator (set once in on_device_ready) so the user
        // always knows which player slot this controller is in.
        if (d->report_parser.set_home_led != NULL) {
            d->report_parser.set_home_led(d, mouse_mode[idx] ? 1 : 0);
        }
    }

    if (mouse_mode[idx]) {
        // Either stick drives the cursor — sums of left + right so one-handed
        // use works either way. A = left click, B = right click. Suppress
        // XInput so games don't see ghost input from the same stick.
        int dx = (int)stick_to_mouse_delta(state.lx) + (int)stick_to_mouse_delta(state.rx);
        int dy = (int)stick_to_mouse_delta(state.ly) + (int)stick_to_mouse_delta(state.ry);
        if (dx > 127)  dx = 127;
        if (dx < -127) dx = -127;
        if (dy > 127)  dy = 127;
        if (dy < -127) dy = -127;
        bool lmb = (state.buttons & BRIDGE_BTN_A) != 0;
        bool rmb = (state.buttons & BRIDGE_BTN_B) != 0;
        mouse_send((int8_t)dx, (int8_t)dy, lmb, rmb);

        bridge_gp_state_t neutral = {0};
        usb_xinput_set_gamepad((uint8_t)idx, &neutral);
    } else {
        usb_xinput_set_gamepad((uint8_t)idx, &state);
    }

    // Pump XInput rumble commands back to the physical controller.
    uint8_t strong, weak;
    if (usb_xinput_poll_rumble((uint8_t)idx, &strong, &weak)) {
        if (d->report_parser.play_dual_rumble != NULL) {
            d->report_parser.play_dual_rumble(d, 0, 250, weak, strong);
        }
    }
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
