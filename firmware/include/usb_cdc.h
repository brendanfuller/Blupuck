#ifndef USB_CDC_H
#define USB_CDC_H

// Reads newline-terminated commands from the always-on CDC debug port and
// dispatches them through a caller-supplied handler. Lets the bridge stay
// out of the bluepad32 / btstack headers (which conflict with tinyusb
// HID symbols).

typedef void (*usb_cdc_command_fn)(const char* line);

void usb_cdc_init(usb_cdc_command_fn handler);
void usb_cdc_tick(void);

#endif
