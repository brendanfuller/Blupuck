#include "usb_cdc.h"

#include <tusb.h>

#define LINE_MAX 64

static char line[LINE_MAX];
static uint8_t line_len;
static usb_cdc_command_fn g_handler;

void usb_cdc_init(usb_cdc_command_fn handler) {
    g_handler = handler;
    line_len = 0;
}

void usb_cdc_tick(void) {
    while (tud_cdc_available()) {
        char c = (char)tud_cdc_read_char();
        if (c == '\r' || c == '\n') {
            if (line_len > 0) {
                line[line_len] = 0;
                if (g_handler) g_handler(line);
                line_len = 0;
            }
        } else if (line_len < LINE_MAX - 1) {
            line[line_len++] = c;
        } else {
            // Overflow — drop the line.
            line_len = 0;
        }
    }
}
