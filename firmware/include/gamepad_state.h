#ifndef GAMEPAD_STATE_H
#define GAMEPAD_STATE_H

#include <stdint.h>

// Neutral gamepad state, free of bluepad32 and tinyusb type collisions.
// Used to ferry input across the BT-side / USB-side boundary.

// Bit positions are chosen so that after the Linux HID-input driver remaps our
// generic Button usages onto evdev BTN_* codes, the resulting BTN_* codes land
// in the slots that Chrome's Standard Gamepad mapping expects. The kernel's
// fixed order for a Generic Desktop Gamepad is BTN_SOUTH, BTN_EAST, BTN_C,
// BTN_NORTH, BTN_WEST, BTN_Z, BTN_TL, BTN_TR, BTN_TL2, BTN_TR2, BTN_SELECT,
// BTN_START, BTN_MODE, BTN_THUMBL, BTN_THUMBR — bits 2 and 5 fall on BTN_C/Z
// which have no Standard slot, so we leave them unused.
enum {
    BRIDGE_BTN_A          = 1u << 0,   // BTN_SOUTH  → Standard buttons[0]
    BRIDGE_BTN_B          = 1u << 1,   // BTN_EAST   → Standard buttons[1]
    BRIDGE_BTN_Y          = 1u << 3,   // BTN_NORTH  → Standard buttons[3]
    BRIDGE_BTN_X          = 1u << 4,   // BTN_WEST   → Standard buttons[2]
    BRIDGE_BTN_SHOULDER_L = 1u << 6,   // BTN_TL     → Standard buttons[4]
    BRIDGE_BTN_SHOULDER_R = 1u << 7,   // BTN_TR     → Standard buttons[5]
    BRIDGE_BTN_TRIGGER_L  = 1u << 8,   // BTN_TL2    → Standard buttons[6]
    BRIDGE_BTN_TRIGGER_R  = 1u << 9,   // BTN_TR2    → Standard buttons[7]
    BRIDGE_BTN_BACK       = 1u << 10,  // BTN_SELECT → Standard buttons[8]
    BRIDGE_BTN_START      = 1u << 11,  // BTN_START  → Standard buttons[9]
    BRIDGE_BTN_SYSTEM     = 1u << 12,  // BTN_MODE   → Standard buttons[16] (Home)
    BRIDGE_BTN_THUMB_L    = 1u << 13,  // BTN_THUMBL → Standard buttons[10]
    BRIDGE_BTN_THUMB_R    = 1u << 14,  // BTN_THUMBR → Standard buttons[11]
    BRIDGE_BTN_CAPTURE    = 1u << 15,  // No Standard slot; appears as extra button
};

enum {
    BRIDGE_DPAD_UP    = 1u << 0,
    BRIDGE_DPAD_DOWN  = 1u << 1,
    BRIDGE_DPAD_RIGHT = 1u << 2,
    BRIDGE_DPAD_LEFT  = 1u << 3,
};

typedef struct {
    int16_t  lx, ly, rx, ry;   // stick axes, roughly [-512, 511]
    uint16_t lt, rt;           // triggers, 0..1023
    uint32_t buttons;          // BRIDGE_BTN_* — needs ≥18 bits for System/Capture
    uint8_t  dpad;             // BRIDGE_DPAD_*
} bridge_gp_state_t;

#endif
