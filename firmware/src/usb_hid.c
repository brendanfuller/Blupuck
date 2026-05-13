#include "usb_hid.h"

#include <string.h>
#include <tusb.h>

static hid_gamepad_report_t cached;
static bool dirty;

void usb_hid_init(void) {
    tusb_init();
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

void usb_hid_set_gamepad(uint8_t player, const bridge_gp_state_t* state) {
    if (player != 0) return;

    // XInput convention: sticks on X/Y + Rx/Ry, triggers on Z/Rz.
    hid_gamepad_report_t r = {0};
    r.x  = scale_stick(state->lx);
    r.y  = scale_stick(state->ly);
    r.rx = scale_stick(state->rx);
    r.ry = scale_stick(state->ry);
    r.z  = (int8_t)((state->lt * 255 / 1024) - 128);
    r.rz = (int8_t)((state->rt * 255 / 1024) - 128);
    r.hat = map_hat(state->dpad);
    r.buttons = state->buttons;  // BRIDGE_BTN_* layout matches our HID button bits

    cached = r;
    dirty = true;
}

void usb_hid_tick(void) {
    tud_task();
    if (!tud_hid_ready()) return;
    if (!dirty) return;
    if (tud_hid_report(0, &cached, sizeof(cached))) {
        dirty = false;
    }
}
