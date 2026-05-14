#ifndef BT_HOST_H
#define BT_HOST_H

#include <uni.h>

struct uni_platform* bt_host_get_platform(void);

// Dispatches a single CDC-line command (e.g. "forget", "help"). Implemented
// in bt_host.c so the bluepad32 / btstack-touching code stays in one place;
// callers (usb_cdc.c) don't need to include uni.h.
void bt_host_handle_text_command(const char* cmd);

// Dispatches a single binary WebUSB command (see firmware/include/protocol.h).
// Wired up by main.c which gives usb_webusb its handler pointer.
void bt_host_handle_webusb_command(uint8_t type, const uint8_t* payload, uint16_t len);

#endif
