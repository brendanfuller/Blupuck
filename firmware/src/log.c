#include "log.h"

#include <string.h>
#include <tusb.h>

// Always attempts the write. If the host hasn't asserted DTR or the FIFO is
// full, the overflow is silently dropped — debug logs never block.
static void write_chunk(const char* s, size_t len) {
    size_t written = 0;
    while (written < len) {
        uint32_t avail = tud_cdc_write_available();
        if (avail == 0) {
            tud_cdc_write_flush();
            return;
        }
        size_t take = len - written;
        if (take > avail) take = avail;
        tud_cdc_write(s + written, (uint32_t)take);
        written += take;
    }
    tud_cdc_write_flush();
}

void log_str(const char* s) {
    write_chunk(s, strlen(s));
}

void log_line(const char* s) {
    write_chunk(s, strlen(s));
    write_chunk("\r\n", 2);
}

void log_u(uint32_t v) {
    char buf[11];
    int i = sizeof(buf);
    buf[--i] = 0;
    if (v == 0) {
        buf[--i] = '0';
    } else {
        while (v) {
            buf[--i] = (char)('0' + (v % 10));
            v /= 10;
        }
    }
    write_chunk(&buf[i], sizeof(buf) - 1 - i);
}

void log_hex(uint32_t v) {
    char buf[11];
    buf[0] = '0';
    buf[1] = 'x';
    for (int i = 0; i < 8; i++) {
        uint8_t nyb = (v >> ((7 - i) * 4)) & 0xF;
        buf[2 + i] = (char)(nyb < 10 ? '0' + nyb : 'a' + (nyb - 10));
    }
    write_chunk(buf, 10);
}
