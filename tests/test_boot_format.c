#include "format.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>

int main(void)
{
    char text[100];
    struct { char before; char value[5]; char after; } bounded = {'a', {0}, 'z'};
    boot_format(text, sizeof(text), "%s %3u%%", "Pass", 100u);
    assert(!strcmp(text, "Pass 100%"));
    boot_format(text, sizeof(text), "%-7s %08x %u", "off", 0x89abcdefu, 4294967295u);
    assert(!strcmp(text, "off     89ABCDEF 4294967295"));
    boot_format(text, sizeof(text), "%5u:%02u:%02u %7s", 0u, 2u, 5u, "none");
    assert(!strcmp(text, "    0:02:05    none"));
    boot_format(bounded.value, sizeof(bounded.value), "%s", "abcdefgh");
    assert(!strcmp(bounded.value, "abcd") && bounded.before == 'a' && bounded.after == 'z');
    boot_format(bounded.value, 0, "unused %u", 1u);
    assert(!strcmp(bounded.value, "abcd"));
    boot_format(text, sizeof(text), "%08x %u", 0u, 0u);
    assert(!strcmp(text, "00000000 0"));
    puts("Boot formatter: progress, alignment, numeric limits and bounds PASS");
    return 0;
}
