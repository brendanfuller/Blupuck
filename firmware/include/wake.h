#ifndef WAKE_H
#define WAKE_H

#include <stdbool.h>
#include <stdint.h>

// Boot-time wake source. Sends a 1-pixel relative-mouse-move HID report to
// the host, optionally after a USB remote-wake signal so a suspended host
// resumes first. Called from usb_xinput when the bridge transitions from
// "no controllers connected" to "first controller present" — subsequent
// controllers don't trigger another wake.

void wake_trigger(void);
void wake_tick(void);  // called from main USB tick

// Send a single relative-move HID mouse report. Used by the controller
// "mouse mode" feature in bt_host.c. Returns false if the endpoint wasn't
// ready (caller can drop and try next tick).
bool mouse_send(int8_t dx, int8_t dy, bool btn_left, bool btn_right);

#endif
