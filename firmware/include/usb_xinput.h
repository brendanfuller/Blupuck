#ifndef USB_XINPUT_H
#define USB_XINPUT_H

#include <stdbool.h>
#include <stdint.h>

#include "gamepad_state.h"

#define BRIDGE_MAX_PLAYERS 4

void usb_xinput_init(void);
void usb_xinput_tick(void);

// Tell the receiver-emulation layer that slot N now has / no longer has a
// connected controller. Triggers a presence-change frame to the host.
void usb_xinput_set_controller_present(uint8_t slot, bool present);

// Latest controller state for slot N. Translated into the 20-byte Xbox 360
// input report on the way out.
void usb_xinput_set_gamepad(uint8_t slot, const bridge_gp_state_t* state);

// Pick up the last XInput rumble command the host sent for slot N, if any.
// Returns true and writes strong/weak (0..255) the first time after a fresh
// command, then false until the host sends another.
bool usb_xinput_poll_rumble(uint8_t slot, uint8_t* strong, uint8_t* weak);

#endif
