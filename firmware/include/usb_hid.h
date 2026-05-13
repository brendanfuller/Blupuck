#ifndef USB_HID_H
#define USB_HID_H

#include <stdint.h>

#include "gamepad_state.h"

void usb_hid_init(void);
void usb_hid_tick(void);  // calls tud_task() + ships any pending report

// Cache the latest report for a given player slot. Step 4: only slot 0 is used.
void usb_hid_set_gamepad(uint8_t player, const bridge_gp_state_t* state);

#endif
