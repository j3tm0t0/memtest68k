#include "format.h"
#include <stdarg.h>

/* Small, bounded formatter. All numeric arguments must be unsigned (32-bit). */
void boot_format(char *out, unsigned capacity, const char *fmt, ...)
{
    unsigned n = 0;
    va_list args;
    if (!capacity) return;
    va_start(args, fmt);
    while (*fmt && n+1 < capacity) {
        if (*fmt++ != '%') { out[n++] = fmt[-1]; continue; }
        if (*fmt == '%') { out[n++] = *fmt++; continue; }
        {
            unsigned width = 0, len = 0, base, value;
            char digits[32], pad = ' ';
            int left = 0;
            const char *s;
            if (*fmt == '-') { left = 1; ++fmt; }
            if (*fmt == '0') { pad = '0'; ++fmt; }
            while (*fmt >= '0' && *fmt <= '9') width = width*10 + (unsigned)(*fmt++-'0');
            if (*fmt == 's') {
                s = va_arg(args, const char *);
                while (s[len]) ++len;
            } else {
                base = *fmt == 'x' ? 16u : 10u;
                value = va_arg(args, unsigned);
                do { digits[len++] = "0123456789ABCDEF"[value % base]; value /= base; } while (value);
                for (value = 0; value < len/2; ++value) {
                    char tmp = digits[value]; digits[value] = digits[len-1-value]; digits[len-1-value] = tmp;
                }
                s = digits;
            }
            if (*fmt) ++fmt;
            if (!left) while (width > len && n+1 < capacity) { out[n++] = pad; --width; }
            for (value = 0; value < len && n+1 < capacity; ++value) out[n++] = s[value];
            if (left) while (width > len && n+1 < capacity) { out[n++] = ' '; --width; }
        }
    }
    out[n] = 0;
    va_end(args);
}
