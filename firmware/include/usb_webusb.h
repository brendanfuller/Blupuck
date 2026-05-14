#ifndef USB_WEBUSB_H
#define USB_WEBUSB_H

// Vendor-class WebUSB transport. Browsers open the interface via
// navigator.usb.requestDevice() filtered on the bridge's VID/PID, then
// claim interface ITF_WEBUSB to bulk-transfer frames. Protocol layer lives
// in firmware/include/protocol.h (shared with webui/src/protocol/).

void usb_webusb_init(void);
void usb_webusb_tick(void);  // ticked from the main USB tick

#endif
