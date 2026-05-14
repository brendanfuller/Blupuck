#include "wake.h"

#include <class/hid/hid_device.h>
#include <pico/time.h>
#include <tusb.h>

#include "log.h"

static bool pulse_pending;
static uint32_t pulse_send_at_ms;

void wake_trigger(void) {
    log_line("wake: trigger (first controller connected)");
    // If the host is suspended, signal a USB remote wakeup first so it
    // resumes. Once resumed, a HID input report on the mouse interface
    // matches the "wake on mouse activity" Windows / Linux policy.
    if (tud_suspended()) {
        tud_remote_wakeup();
    }
    pulse_pending = true;
    // Wait a little so the host has a chance to process resume before we
    // queue the report. Harmless if the host was already awake.
    pulse_send_at_ms = to_ms_since_boot(get_absolute_time()) + 150;
}

void wake_tick(void) {
    if (!pulse_pending) return;
    if (to_ms_since_boot(get_absolute_time()) < pulse_send_at_ms) return;
    if (!tud_hid_ready()) {
        // HID endpoint not yet idle; try again next tick.
        pulse_send_at_ms = to_ms_since_boot(get_absolute_time()) + 10;
        return;
    }

    hid_mouse_report_t report = {0};
    report.x = 1;  // single pixel relative move
    if (tud_hid_report(0, &report, sizeof(report))) {
        log_line("wake: mouse pulse sent");
        pulse_pending = false;
    }
}

bool mouse_send(int8_t dx, int8_t dy, bool btn_left, bool btn_right) {
    if (!tud_hid_ready()) return false;
    uint8_t buttons = 0;
    if (btn_left)  buttons |= MOUSE_BUTTON_LEFT;
    if (btn_right) buttons |= MOUSE_BUTTON_RIGHT;
    hid_mouse_report_t report = {
        .buttons = buttons,
        .x = dx,
        .y = dy,
        .wheel = 0,
        .pan = 0,
    };
    return tud_hid_report(0, &report, sizeof(report));
}
