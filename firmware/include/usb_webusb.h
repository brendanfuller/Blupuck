#ifndef USB_WEBUSB_H
#define USB_WEBUSB_H

#include <stdbool.h>
#include <stdint.h>

// Vendor-class WebUSB transport. Browsers open the interface via
// navigator.usb.requestDevice() filtered on the bridge's VID/PID, then
// claim Interface ITF_WEBUSB to bulk-transfer frames.
// Wire format: see firmware/include/protocol.h.

typedef void (*usb_webusb_cmd_fn)(uint8_t type, const uint8_t* payload, uint16_t len);

void usb_webusb_init(usb_webusb_cmd_fn handler);
void usb_webusb_tick(void);

// Send a single framed message. Returns false if the device isn't mounted
// or the TX FIFO can't accept the whole frame; caller can retry next tick.
bool usb_webusb_send(uint8_t type, const uint8_t* payload, uint16_t len);

#endif
