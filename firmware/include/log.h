#ifndef LOG_H
#define LOG_H

#include <stdint.h>

// Debug serial out over the always-present CDC interface (/dev/ttyACM* on Linux).
// All writes are non-blocking and silently dropped if no host is reading.

void log_str(const char* s);
void log_u(uint32_t v);
void log_hex(uint32_t v);
void log_line(const char* s);  // log_str(s) + "\r\n"

#endif
