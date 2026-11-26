#include <stddef.h>

/* Minimal freestanding support; never pulls in Human68k/newlib startup. */
void *memset(void *dest, int value, size_t n)
{
    unsigned char *p = dest;
    while (n--) *p++ = (unsigned char)value;
    return dest;
}

void *memcpy(void *dest, const void *source, size_t n)
{
    unsigned char *d = dest;
    const unsigned char *s = source;
    while (n--) *d++ = *s++;
    return dest;
}
