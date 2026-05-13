#include "usb_hid.h"

#include <string.h>
#include <tusb.h>

#include "log.h"

// One TinyUSB HID instance per bluepad32 slot. Static 1:1 mapping —
// usb_hid_set_gamepad(N, ...) routes to tud_hid_n_report(N, ...).

static hid_gamepad_report_t cached[BRIDGE_MAX_PLAYERS];
static bool dirty[BRIDGE_MAX_PLAYERS];

void usb_hid_init(void) {
    tusb_init();
}

// No longer drives descriptor selection (all 4 are always enumerated) but
// kept as a placeholder in case future code wants the count.
uint8_t usb_hid_enumerated_count(void) {
    return BRIDGE_MAX_PLAYERS;
}

void usb_hid_set_controller_present(uint8_t slot, bool present) {
    if (slot >= BRIDGE_MAX_PLAYERS) return;
    log_str("usb: slot ");
    log_u(slot);
    log_line(present ? " present" : " absent");

    if (!present) {
        // Release any held inputs on the host side by sending one zeroed report.
        memset(&cached[slot], 0, sizeof(cached[slot]));
        cached[slot].hat = GAMEPAD_HAT_CENTERED;
        dirty[slot] = true;
    }
}

static inline int8_t scale_stick(int16_t v) {
    int32_t s = v / 4;
    if (s > 127) return 127;
    if (s < -127) return -127;
    return (int8_t)s;
}

static uint8_t map_hat(uint8_t dpad) {
    const bool up    = dpad & BRIDGE_DPAD_UP;
    const bool down  = dpad & BRIDGE_DPAD_DOWN;
    const bool left  = dpad & BRIDGE_DPAD_LEFT;
    const bool right = dpad & BRIDGE_DPAD_RIGHT;

    if (up    && right) return GAMEPAD_HAT_UP_RIGHT;
    if (down  && right) return GAMEPAD_HAT_DOWN_RIGHT;
    if (down  && left)  return GAMEPAD_HAT_DOWN_LEFT;
    if (up    && left)  return GAMEPAD_HAT_UP_LEFT;
    if (up)             return GAMEPAD_HAT_UP;
    if (right)          return GAMEPAD_HAT_RIGHT;
    if (down)           return GAMEPAD_HAT_DOWN;
    if (left)           return GAMEPAD_HAT_LEFT;
    return GAMEPAD_HAT_CENTERED;
}

void usb_hid_set_gamepad(uint8_t slot, const bridge_gp_state_t* state) {
    if (slot >= BRIDGE_MAX_PLAYERS) return;

    // XInput convention: sticks on X/Y + Rx/Ry, triggers on Z/Rz.
    hid_gamepad_report_t r = {0};
    r.x  = scale_stick(state->lx);
    r.y  = scale_stick(state->ly);
    r.rx = scale_stick(state->rx);
    r.ry = scale_stick(state->ry);
    r.z  = (int8_t)((state->lt * 255 / 1024) - 128);
    r.rz = (int8_t)((state->rt * 255 / 1024) - 128);
    r.hat = map_hat(state->dpad);
    r.buttons = state->buttons;

    cached[slot] = r;
    dirty[slot] = true;
}

void usb_hid_tick(void) {
    tud_task();

    for (int slot = 0; slot < BRIDGE_MAX_PLAYERS; slot++) {
        if (!dirty[slot]) continue;
        if (!tud_hid_n_ready((uint8_t)slot)) continue;
        if (tud_hid_n_report((uint8_t)slot, 0, &cached[slot], sizeof(cached[slot]))) {
            dirty[slot] = false;
        }
    }
}
