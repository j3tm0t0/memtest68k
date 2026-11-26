/* Human68k front end. The memory-test engine has no OS dependencies. */
#include "memtest.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <x68k/dos.h>
#include <x68k/iocs.h>

#define VERSION "0.1"
#define RAM_END UINT32_C(0x00c00000)
#define HISTORY 12

/* Override newlib's startup defaults before it releases the program block. */
int _stack_size = 16384;
int _heap_size = 32768;

typedef struct {
    unsigned mask, pass_mask, test_index, tests_in_pass;
    uint32_t passes, pass_limit, start_ticks, last_draw;
    unsigned long limit_kib, bytes, installed;
    int plain, quit, frozen, cpu, old_cache;
    const char *cache;
    mt_region regions[MT_MAX_REGIONS];
    size_t count;
    mt_context ctx;
    mt_progress current;
    mt_error history[HISTORY];
    uint32_t error_pass[HISTORY];
    unsigned history_next, history_count;
    int mode, color, palettes[4];
} app;

static app a;
static char screen_text[32][96];
static unsigned char screen_color[32][96];

static uint32_t ticks(void)
{
    struct iocs_time t = _iocs_ontime();
    return (uint32_t)t.day * UINT32_C(8640000) + (uint32_t)t.sec;
}

static void field(int x, int y, int width, int color, const char *s)
{
    char buf[97];
    int first = 0, last;
    size_t len = strlen(s);
    if (width > 95-x) width = 95-x; /* Never write the lower-right cell. */
    if (len > (size_t)width) len = (size_t)width;
    memset(buf, ' ', (size_t)width);
    memcpy(buf, s, len);
    buf[width] = '\0';
    while (first < width && screen_text[y][x+first] == buf[first] &&
           screen_color[y][x+first] == color) ++first;
    if (first == width) return;
    last = width-1;
    while (last > first && screen_text[y][x+last] == buf[last] &&
           screen_color[y][x+last] == color) --last;
    memcpy(&screen_text[y][x], buf, (size_t)width);
    memset(&screen_color[y][x], color, (size_t)width);
    buf[last+1] = '\0';
    _iocs_b_locate(x+first, y);
    _iocs_b_color(color);
    _iocs_b_print(buf+first);
}

static void rule(int y)
{
    field(0, y, 95, 3,
          "-----------------------------------------------------------------------------------------------");
}

static unsigned bits(unsigned n)
{
    unsigned result = 0;
    while (n) { result += n & 1u; n >>= 1; }
    return result;
}

static unsigned test_percent(void)
{
    const mt_progress *p = &a.current;
    unsigned within = p->total ? (unsigned)(p->done * 100u / p->total) : 0;
    return p->stages ? (p->stage * 100u + within) / p->stages : 0;
}

static void bar(int row, const char *name, unsigned percent)
{
    char buf[62];
    unsigned i, n = percent * 40u / 100u;
    snprintf(buf, sizeof(buf), "%s %3u%% ", name, percent);
    for (i = 0; i < 40; ++i) buf[10+i] = i < n ? '#' : ' ';
    buf[50] = '\0';
    field(37, row, 58, 3, buf);
}

static void draw_errors(void)
{
    unsigned i;
    char buf[112];
    if (a.frozen) return;
    for (i = 0; i < HISTORY; ++i) {
        if (i < a.history_count) {
            unsigned n = (a.history_next + HISTORY - a.history_count + i) % HISTORY;
            const mt_error *e = &a.history[n];
            snprintf(buf, sizeof(buf), "%5lu  %2u    %08lX     %08lX   %08lX   %08lX",
                     (unsigned long)a.error_pass[n], e->test+1,
                     (unsigned long)e->address, (unsigned long)e->expected,
                     (unsigned long)e->actual, (unsigned long)(e->expected ^ e->actual));
            field(0, 17+(int)i, 95, 2+8, buf);
        } else field(0, 17+(int)i, 95, 3, "");
    }
    if (!a.history_count)
        field(0, 18, 95, 3, "  No errors detected. Testing continues until you press ESC.");
}

static void draw_status(const char *message)
{
    char buf[128];
    uint32_t seconds = (ticks()-a.start_ticks)/100;
    unsigned tp = test_percent();
    unsigned pp = a.tests_in_pass ? (a.test_index*100u+tp)/a.tests_in_pass : 0;
    if (pp > 100) pp = 100;
    bar(0, "Pass", pp);
    bar(1, "Test", tp);
    snprintf(buf, sizeof(buf), "Test #%u [%s]", a.current.test+1,
             mt_test_names[a.current.test]);
    field(37, 2, 58, 3, buf);
    snprintf(buf, sizeof(buf), "Testing: %08lX  (%luK in %u blocks)",
             (unsigned long)a.current.address, a.bytes/1024, (unsigned)a.count);
    field(37, 3, 58, 3, buf);
    snprintf(buf, sizeof(buf), "Pattern: %08lX  Step %u/%u",
             (unsigned long)a.current.pattern, a.current.stage+1, a.current.stages);
    field(37, 4, 58, 3, buf);
    field(37, 5, 58, 3, a.current.operation ? a.current.operation : "Starting tests");
    snprintf(buf, sizeof(buf), "%5lu:%02lu:%02lu  %7luK  %7luK  Human68k   %-7s %4u %7lu %10lu",
             (unsigned long)(seconds/3600), (unsigned long)(seconds/60%60),
             (unsigned long)(seconds%60), a.bytes/1024,
             a.installed > a.bytes ? (a.installed-a.bytes)/1024 : 0,
             a.cache, bits(a.pass_mask), (unsigned long)a.passes,
             (unsigned long)a.ctx.errors);
    field(0, 12, 95, 3, buf);
    field(0, 30, 95, a.ctx.errors ? 10 : 3, message);
    draw_errors();
}

static void draw_screen(void)
{
    unsigned i;
    char buf[100];
    for (i = 0; i < 32; ++i) field(0, (int)i, 95, 3, "");
    field(0, 0, 35, 9, "         Memtest68 v" VERSION);
    snprintf(buf, sizeof(buf), "Motorola 680%02d / X680x0", a.cpu == 6 ? 60 : a.cpu*10);
    field(0, 1, 35, 3, buf);
    snprintf(buf, sizeof(buf), "Main RAM : %7luK", a.installed/1024);
    field(0, 3, 35, 3, buf);
    snprintf(buf, sizeof(buf), "Test RAM : %7luK", a.bytes/1024);
    field(0, 4, 35, 3, buf);
    snprintf(buf, sizeof(buf), "D-Cache  : %s", a.cache);
    field(0, 5, 35, 3, buf);
    for (i = 0; i < 7; ++i) field(35, (int)i, 1, 3, "|");
    field(37, 6, 58, 3, "----------------------------------------------------------");
    field(0, 7, 95, 3, "Memory map: DOS allocated conventional RAM only (below $00C00000)");
    field(0, 8, 95, 3, "Excluded: OS, drivers, resident programs, Memtest68 and unselected RAM");
    field(0, 10, 95, 3, "  WallTime   Tested    Excluded  MemMap     D-Cache Test    Pass     Errors");
    rule(11); rule(13);
    field(0, 15, 95, 3, " Pass  Test   Failing address  Good       Bad        Err-Bits");
    rule(16); rule(29);
    field(0, 31, 95, 11, "(ESC) Exit to DOS  (c) Configuration  (SP) Scroll lock  (CR) Unlock  (p) Pause");
    draw_status("Testing memory. Press c to configure tests for the next pass.");
}

static void ui_open(void)
{
    int i;
    a.color = _iocs_b_color(-1);
    a.mode = _iocs_crtmod(-1);
    for (i = 0; i < 4; ++i) a.palettes[i] = _iocs_tpalet(i, -1);
    _iocs_crtmod(16); /* 768 x 512, 96 x 32 text. */
    _iocs_tpalet(0, 0x003e); /* Blue background. */
    _iocs_tpalet(1, 0xf800); /* Green title. */
    _iocs_tpalet(2, 0xffc0); /* Yellow error field, high contrast on blue. */
    _iocs_tpalet(3, 0xfffe);
    _dos_c_print("\033[2J\033[H");
    _dos_c_curoff();
    memset(screen_color, 255, sizeof(screen_color));
    draw_screen();
}

static void ui_close(void)
{
    int i;
    _iocs_crtmod(a.mode);
    for (i = 0; i < 4; ++i) _iocs_tpalet(i, a.palettes[i]);
    _dos_c_print("\033[2J\033[H");
    _iocs_b_color(a.color);
    _dos_c_curon();
}

static int key(void)
{
    if (!_iocs_b_keysns()) return 0;
    return _iocs_b_keyinp() & 255;
}

static void configure(void)
{
    int k;
    unsigned i;
    char buf[100];
    do {
        field(0, 15, 95, 9, "  Configuration - changes take effect at the NEXT pass");
        for (i = 0; i < MT_TEST_COUNT; ++i) {
            snprintf(buf, sizeof(buf), "  (%u) [%c] %s", i+1,
                     a.mask & (1u << i) ? 'X' : ' ', mt_test_names[i]);
            field(0, 17+(int)i, 95, 3, buf);
        }
        field(0, 25, 95, 3, "  1-7: toggle test    a: select all    ENTER: resume    ESC: exit to DOS");
        field(0, 26, 95, 3, "  At least one test must be selected. Error counters are preserved.");
        do { k = _iocs_b_keyinp() & 255; } while (!k);
        if (k >= '1' && k <= '7') a.mask ^= 1u << (k-'1');
        if (k == 'a' || k == 'A') a.mask = MT_ALL_TESTS;
        if (k == 27) { a.quit = 1; break; }
    } while (k != 13 || !a.mask);
    a.frozen = 0;
    draw_screen();
}

static int on_progress(void *user, const mt_progress *p)
{
    uint32_t now;
    int k;
    (void)user;
    a.current = *p;
    if (a.plain) return a.quit;
    k = key();
    if (k == 27 || k == 'q' || k == 'Q') a.quit = 1;
    if (k == 'c' || k == 'C') configure();
    if (k == ' ') a.frozen = !a.frozen;
    if (k == 13) a.frozen = 0;
    if (k == 'p' || k == 'P') {
        draw_status("PAUSED - press p to continue, ESC to exit to DOS.");
        do { k = _iocs_b_keyinp() & 255; } while (k != 'p' && k != 'P' && k != 27);
        if (k == 27) a.quit = 1;
    }
    now = ticks();
    if (now-a.last_draw >= 20 || !p->done || p->done == p->total) {
        a.last_draw = now;
        draw_status(a.frozen ? "SCROLL LOCK - counters continue; ENTER unlocks the error list." :
                    a.passes ? "Pass complete. Testing continues; press ESC to exit to DOS." :
                               "Testing memory. Press c to configure tests for the next pass.");
    }
    return a.quit;
}

static void on_error(void *user, const mt_error *e)
{
    unsigned n = a.history_next;
    (void)user;
    a.history[n] = *e;
    a.error_pass[n] = a.passes+1;
    a.history_next = (n+1) % HISTORY;
    if (a.history_count < HISTORY) ++a.history_count;
    if (a.plain && a.ctx.errors <= 32)
        printf("ERROR pass=%lu test=%u address=%08lX good=%08lX bad=%08lX xor=%08lX\n",
               (unsigned long)a.passes+1, e->test+1, (unsigned long)e->address,
               (unsigned long)e->expected, (unsigned long)e->actual,
               (unsigned long)(e->expected ^ e->actual));
}

static void release(void)
{
    while (a.count) _dos_mfree((void *)a.regions[--a.count].base);
}

static int acquire(void)
{
    unsigned long remaining = a.limit_kib ? a.limit_kib*1024 : RAM_END;
    while (a.count < MT_MAX_REGIONS && remaining >= 4) {
        uintptr_t query = (uintptr_t)_dos_malloc(0x00ffffff);
        uintptr_t address;
        unsigned long size;
        void *block;
        if ((query & UINT32_C(0xff000000)) != UINT32_C(0x81000000)) {
            if (!(query & UINT32_C(0x80000000)) && query) {
                _dos_mfree((void *)query);
                return -1;
            }
            break;
        }
        size = (unsigned long)(query & UINT32_C(0x00fffffc));
        if (size > remaining) size = remaining;
        if (size < 4) break;
        block = _dos_malloc((int)size);
        address = (uintptr_t)block;
        if (!address || (address & UINT32_C(0x80000000))) return -1;
        if ((address & 3) || address >= RAM_END || size > RAM_END-address) {
            _dos_mfree(block);
            return -1;
        }
        a.regions[a.count].base = block;
        a.regions[a.count++].words = size/4;
        a.bytes += size;
        remaining -= size;
    }
    return !a.count || (a.limit_kib && remaining) ? -1 : 0;
}

static void usage(void)
{
    puts("Memtest68 " VERSION " - X680x0 / Human68k memory tester\n"
         "Usage: memtest68 [--passes N] [--size KiB] [--tests 1234567] [--plain]\n"
         "Default: all DOS-allocatable conventional RAM, all tests, repeat forever.\n"
         "  --passes N   Stop after N complete passes (0 = forever).\n"
         "  --size KiB   Limit tested memory (1..12288 KiB).\n"
         "  --tests LIST Select test numbers, e.g. 126.\n"
         "  --plain      Text log; --passes is required and must be nonzero.\n"
         "  --help       Show help.\n"
         "Screen: ESC exit, c configuration, SPACE scroll lock, ENTER unlock, p pause.\n"
         "Tests: 1 address, 2 inversions 0/1, 3 checkerboard, 4 walking bits,\n"
         "       5 random, 6 March C-, 7 byte/word access.\n"
         "Only allocated RAM is overwritten. OS, drivers and this program are excluded.");
}

static int number(const char *s, unsigned long maximum, unsigned long *value)
{
    unsigned long n = 0;
    if (!*s) return -1;
    while (*s) {
        unsigned d = (unsigned)(*s++ - '0');
        if (d > 9 || n > maximum/10 || (n == maximum/10 && d > maximum%10)) return -1;
        n = n*10+d;
    }
    *value = n;
    return 0;
}

int main(int argc, char **argv)
{
    int i, result = 0, old_break;
    unsigned test;
    unsigned long n;
    a.mask = MT_ALL_TESTS;
    a.old_cache = -1;
    a.cache = "none";
    for (i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--help") || !strcmp(argv[i], "-h")) { usage(); return 0; }
        if (!strcmp(argv[i], "--plain")) { a.plain = 1; continue; }
        if (i+1 == argc) { usage(); return 2; }
        if (!strcmp(argv[i], "--passes")) {
            if (number(argv[++i], UINT32_MAX-1, &n)) return 2;
            a.pass_limit = (uint32_t)n;
        } else if (!strcmp(argv[i], "--size")) {
            if (number(argv[++i], 12288, &n) || !n) return 2;
            a.limit_kib = n;
        } else if (!strcmp(argv[i], "--tests")) {
            const char *s = argv[++i];
            a.mask = 0;
            while (*s) {
                if (*s < '1' || *s > '7') return 2;
                a.mask |= 1u << (*s++-'1');
            }
            if (!a.mask) return 2;
        } else { usage(); return 2; }
    }
    if (a.plain && !a.pass_limit) { usage(); return 2; }
    /* Warm up stdio before reserving test RAM; leave newlib a private heap. */
    printf("Memtest68 " VERSION " - preparing memory tests...\n");
    fflush(stdout);
    a.cpu = _iocs_b_bpeek((const void *)0x0cbc) & 255;
    if (a.cpu != 0 && a.cpu != 1 && a.cpu != 2 && a.cpu != 3) {
        puts("This release supports 68000/68010/68020/68030. Accelerator cache control is not implemented.");
        return 2;
    }
    /* SRAM holds the configured conventional RAM size in bytes.
     * This is display-only; allocation bounds are checked independently. */
    if (!a.plain) a.installed = (unsigned long)_iocs_b_lpeek((const void *)0xed0008);
    if (a.installed > RAM_END) a.installed = RAM_END;
    if (acquire()) {
        release();
        puts("Cannot allocate the requested conventional RAM. Try a smaller --size.");
        return 2;
    }
    if (a.cpu == 3) {
        a.old_cache = _iocs_cache_st();
        if (a.old_cache < 0 || _iocs_cache_md(a.old_cache & 1) < 0 ||
            (_iocs_cache_st() & 2)) {
            if (a.old_cache >= 0) _iocs_cache_md(a.old_cache);
            release();
            puts("Cannot disable the 68030 data cache. Tests were not started.");
            return 2;
        }
        a.cache = "off";
    }
    old_break = _dos_breakck(0);
    a.ctx.progress = on_progress;
    a.ctx.error = on_error;
    a.start_ticks = ticks();
    a.current.operation = "Preparing first pass";
    a.current.stages = 1;
    a.pass_mask = a.mask;
    if (!a.plain) ui_open();
    else {
        printf("Testing %lu KiB in %u DOS-allocated blocks; D-cache=%s\n",
               a.bytes/1024, (unsigned)a.count, a.cache);
        for (test = 0; test < a.count; ++test)
            printf("RAM %08lX-%08lX\n", (unsigned long)a.regions[test].base,
                   (unsigned long)a.regions[test].base+a.regions[test].words*4-1);
    }
    while (!a.quit && (!a.pass_limit || a.passes < a.pass_limit)) {
        a.pass_mask = a.mask;
        a.tests_in_pass = bits(a.pass_mask);
        a.test_index = 0;
        for (test = 0; test < MT_TEST_COUNT; ++test) {
            int status;
            if (!(a.pass_mask & (1u << test))) continue;
            if (a.plain) printf("Pass %lu Test %u: %s\n", (unsigned long)a.passes+1,
                                test+1, mt_test_names[test]);
            status = mt_run(&a.ctx, a.regions, a.count, test,
                            UINT32_C(0x68c0ffee) ^ a.passes);
            if (status) { a.quit = 1; result = status < 0 ? 2 : 130; break; }
            ++a.test_index;
        }
        if (!a.quit) {
            if (a.passes != UINT32_MAX) ++a.passes;
            if (a.plain) printf("Pass %lu complete, errors=%lu\n", (unsigned long)a.passes,
                                (unsigned long)a.ctx.errors);
        }
    }
    if (!a.plain && !a.quit) {
        a.current.stage = a.current.stages-1;
        a.current.done = a.current.total;
        a.test_index = a.tests_in_pass-1;
        draw_status(a.ctx.errors ? "Testing complete - ERRORS DETECTED. Press ESC to exit to DOS." :
                                  "***** Pass complete, no errors. Press ESC to exit to DOS. *****");
        while ((i = _iocs_b_keyinp() & 255) != 27 && i != 'q' && i != 'Q') {}
    }
    if (!a.plain) ui_close();
    if (a.old_cache >= 0) _iocs_cache_md(a.old_cache);
    _dos_breakck(old_break);
    release();
    printf("Memtest68: %s; completed passes=%lu, errors=%lu, tested=%lu KiB\n",
           result == 130 ? "stopped" : result || a.ctx.errors ? "FAIL" : "PASS",
           (unsigned long)a.passes, (unsigned long)a.ctx.errors, a.bytes/1024);
    if (a.ctx.errors) return 1;
    return result;
}
