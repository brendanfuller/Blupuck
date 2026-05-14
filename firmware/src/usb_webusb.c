#include "usb_webusb.h"

#include <string.h>
#include <tusb.h>

#include "log.h"

// Stream parser for incoming bulk data. Frames are [type:u8][len:u16][payload];
// we accumulate until a full frame is buffered, dispatch to the handler, then
// shift the remainder forward.

#define RX_BUF_MAX 512

static uint8_t rx_buf[RX_BUF_MAX];
static uint16_t rx_len;
static usb_webusb_cmd_fn g_handler;

void usb_webusb_init(usb_webusb_cmd_fn handler) {
    g_handler = handler;
    rx_len = 0;
}

bool usb_webusb_send(uint8_t type, const uint8_t* payload, uint16_t len) {
    if (!tud_vendor_mounted()) return false;
    uint8_t hdr[3] = {type, (uint8_t)(len & 0xff), (uint8_t)((len >> 8) & 0xff)};
    uint32_t avail = tud_vendor_write_available();
    if (avail < (uint32_t)(3 + len)) return false;
    tud_vendor_write(hdr, 3);
    if (len > 0) tud_vendor_write(payload, len);
    tud_vendor_write_flush();
    return true;
}

void usb_webusb_tick(void) {
    uint8_t tmp[64];
    while (tud_vendor_available()) {
        uint32_t n = tud_vendor_read(tmp, sizeof(tmp));
        if (n == 0) break;
        if (rx_len + n > RX_BUF_MAX) {
            // Buffer overflow — reset and drop to recover.
            log_line("webusb: rx overflow, resync");
            rx_len = 0;
            continue;
        }
        memcpy(rx_buf + rx_len, tmp, n);
        rx_len += (uint16_t)n;
    }

    // Parse as many complete frames as we have.
    uint16_t cursor = 0;
    while (rx_len - cursor >= 3) {
        uint16_t plen = (uint16_t)rx_buf[cursor + 1] | ((uint16_t)rx_buf[cursor + 2] << 8);
        if (rx_len - cursor < (uint16_t)(3 + plen)) break;
        if (g_handler) g_handler(rx_buf[cursor], &rx_buf[cursor + 3], plen);
        cursor = (uint16_t)(cursor + 3 + plen);
    }
    if (cursor > 0) {
        memmove(rx_buf, rx_buf + cursor, rx_len - cursor);
        rx_len = (uint16_t)(rx_len - cursor);
    }
}
