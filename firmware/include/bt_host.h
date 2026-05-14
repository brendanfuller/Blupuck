#ifndef BT_HOST_H
#define BT_HOST_H

#include <uni.h>

struct uni_platform* bt_host_get_platform(void);

// Dispatches a single CDC-line command (e.g. "forget", "help"). Implemented
// in bt_host.c so the bluepad32 / btstack-touching code stays in one place;
// callers (usb_cdc.c) don't need to include uni.h.
void bt_host_handle_text_command(const char* cmd);

#endif
