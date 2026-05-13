#ifndef TRANSLATE_H
#define TRANSLATE_H

#include <uni.h>

#include "gamepad_state.h"

// bluepad32 gamepad → neutral state. Pure transform, no filtering (spec §6.1).
void translate_gamepad(const uni_gamepad_t* in, bridge_gp_state_t* out);

#endif
