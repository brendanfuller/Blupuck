#include "usb_webusb.h"

#include <string.h>
#include <tusb.h>

#include "log.h"

// Minimal echo-loop for first-light validation. Once the browser can open the
// interface and round-trip bytes, the protocol layer in usb_webusb_tick()
// gets replaced with framed JSON / binary messages.

void usb_webusb_init(void) {
    // Nothing — TinyUSB's vendor class is set up by tusb_init().
}

void usb_webusb_tick(void) {
    uint8_t buf[64];
    while (tud_vendor_available()) {
        uint32_t n = tud_vendor_read(buf, sizeof(buf));
        if (n == 0) break;
        log_str("webusb: rx ");
        log_u(n);
        log_line(" bytes");
        // Echo back so the web UI can confirm the channel is alive.
        tud_vendor_write(buf, n);
        tud_vendor_write_flush();
    }
}
