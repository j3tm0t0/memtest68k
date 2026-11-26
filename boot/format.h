#ifndef BOOT_FORMAT_H
#define BOOT_FORMAT_H

/* Bounded freestanding formatter: %s, %u, %x, %%, widths, zero/left padding.
 * Numeric arguments must be unsigned (32-bit on the target). */
void boot_format(char *out, unsigned capacity, const char *fmt, ...);

#endif
