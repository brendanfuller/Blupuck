#include "translate.h"

#include <string.h>
#include <uni.h>

void translate_gamepad(const uni_gamepad_t* in, bridge_gp_state_t* out) {
    memset(out, 0, sizeof(*out));

    out->lx = (int16_t)in->axis_x;
    out->ly = (int16_t)in->axis_y;
    out->rx = (int16_t)in->axis_rx;
    out->ry = (int16_t)in->axis_ry;
    out->lt = (uint16_t)in->brake;
    out->rt = (uint16_t)in->throttle;

    if (in->dpad & DPAD_UP)    out->dpad |= BRIDGE_DPAD_UP;
    if (in->dpad & DPAD_DOWN)  out->dpad |= BRIDGE_DPAD_DOWN;
    if (in->dpad & DPAD_RIGHT) out->dpad |= BRIDGE_DPAD_RIGHT;
    if (in->dpad & DPAD_LEFT)  out->dpad |= BRIDGE_DPAD_LEFT;

    uint32_t btn = 0;
    if (in->buttons & BUTTON_A)           btn |= BRIDGE_BTN_A;
    if (in->buttons & BUTTON_B)           btn |= BRIDGE_BTN_B;
    if (in->buttons & BUTTON_X)           btn |= BRIDGE_BTN_X;
    if (in->buttons & BUTTON_Y)           btn |= BRIDGE_BTN_Y;
    if (in->buttons & BUTTON_SHOULDER_L)  btn |= BRIDGE_BTN_SHOULDER_L;
    if (in->buttons & BUTTON_SHOULDER_R)  btn |= BRIDGE_BTN_SHOULDER_R;
    if (in->buttons & BUTTON_TRIGGER_L)   btn |= BRIDGE_BTN_TRIGGER_L;
    if (in->buttons & BUTTON_TRIGGER_R)   btn |= BRIDGE_BTN_TRIGGER_R;
    if (in->buttons & BUTTON_THUMB_L)     btn |= BRIDGE_BTN_THUMB_L;
    if (in->buttons & BUTTON_THUMB_R)     btn |= BRIDGE_BTN_THUMB_R;

    if (in->misc_buttons & MISC_BUTTON_SYSTEM)  btn |= BRIDGE_BTN_SYSTEM;
    if (in->misc_buttons & MISC_BUTTON_BACK)    btn |= BRIDGE_BTN_BACK;
    if (in->misc_buttons & MISC_BUTTON_START)   btn |= BRIDGE_BTN_START;
    if (in->misc_buttons & MISC_BUTTON_CAPTURE) btn |= BRIDGE_BTN_CAPTURE;

    out->buttons = btn;
}
