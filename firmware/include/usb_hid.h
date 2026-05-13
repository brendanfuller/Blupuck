#ifndef USB_HID_H
#define USB_HID_H

#include <stdbool.h>
#include <stdint.h>

#include "gamepad_state.h"

#define BRIDGE_MAX_PLAYERS 4

void usb_hid_init(void);
void usb_hid_tick(void);  // ticks tinyusb + handles re-enum + ships reports

// Called by bt_host on controller connect/disconnect. `slot` is the
// bluepad32 device index (0..3).
void usb_hid_set_controller_present(uint8_t slot, bool present);

// Cache the latest report for a controller slot.
void usb_hid_set_gamepad(uint8_t slot, const bridge_gp_state_t* state);

// Number of HID gamepad interfaces currently enumerated (0..4).
// Used by usb_descriptors.c to pick which configuration descriptor to return.
uint8_t usb_hid_enumerated_count(void);

#endif
