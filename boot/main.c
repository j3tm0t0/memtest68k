#include "memtest.h"
#include "format.h"
#include <x68k/iocs.h>

#define HIGH_START UINT32_C(0x10000)
#define MAX_RAM UINT32_C(0xc00000)
#define LOW_TEST 7u
#define LOW_MASK (1u << LOW_TEST)
#define ALL_TESTS (MT_ALL_TESTS | LOW_MASK)
#define HISTORY 12u

#ifdef BOOT_HDS
#define BOOT_MEDIUM "HDS"
#define BOOT_SOURCE "SCSI HDD"
#define BOOT_EXIT_NAME "Restart"
#define BOOT_EXIT_LOWER "restart"
#define BOOT_EXIT_VERB "restarts"
extern void boot_restart_hds(void) __attribute__((__noreturn__));
#else
#define BOOT_MEDIUM "FD"
#define BOOT_SOURCE "FD"
#define BOOT_EXIT_NAME "Reboot"
#define BOOT_EXIT_LOWER "reboot"
#define BOOT_EXIT_VERB "reboots"
#endif

extern const unsigned char low_probe_image_start[], low_probe_image_end[];

static struct {
    uint32_t ram, passes, limit, start, last_draw;
    unsigned mask, active, index, count, cpu;
    int frozen, quit;
    const char *cache;
    mt_context ctx;
    mt_progress current;
    mt_error history[HISTORY];
    uint32_t history_pass[HISTORY], low_errors;
    unsigned next, used;
    char text[32][96];
    unsigned char color[32][96];
} a;

static const char *test_name(unsigned test)
{
    return test == LOW_TEST ? "Low RAM, VRAM relocated" : mt_test_names[test];
}

static unsigned count_bits(unsigned value)
{
    unsigned n = 0;
    while (value) { n += value & 1u; value >>= 1; }
    return n;
}

static uint32_t ticks(void)
{
    struct iocs_time t = _iocs_ontime();
    return (uint32_t)t.day * UINT32_C(8640000) + (uint32_t)t.sec;
}

static void field(unsigned x, unsigned y, unsigned width, unsigned color, const char *text)
{
    char buf[97];
    unsigned i = 0, first = 0, last;
    if (width > 95-x) width = 95-x;
    while (i < width && *text) buf[i++] = *text++;
    while (i < width) buf[i++] = ' ';
    buf[width] = 0;
    while (first < width && a.text[y][x+first] == buf[first] && a.color[y][x+first] == color) ++first;
    if (first == width) return;
    last = width-1;
    while (last > first && a.text[y][x+last] == buf[last] && a.color[y][x+last] == color) --last;
    for (i = 0; i < width; ++i) { a.text[y][x+i] = buf[i]; a.color[y][x+i] = (unsigned char)color; }
    buf[last+1] = 0;
    _iocs_b_color((int)color);
    _iocs_b_locate((int)(x+first), (int)y);
    _iocs_b_print(buf+first);
}

static void rule(unsigned row)
{
    field(0, row, 95, 3, "-----------------------------------------------------------------------------------------------");
}

static unsigned percent(void)
{
    const mt_progress *p = &a.current;
    unsigned fraction = p->total ? (unsigned)(p->done*100u/p->total) : 0;
    return p->stages ? (p->stage*100u+fraction)/p->stages : 0;
}

static void bar(unsigned y, const char *label, unsigned percentage)
{
    char text[64];
    unsigned i;
    boot_format(text, sizeof(text), "%s %3u%% ", label, percentage);
    /* Labels have exactly four characters. */
    for (i = 0; i < 40; ++i) text[10+i] = i < percentage*40u/100u ? '#' : ' ';
    text[50] = 0;
    field(37, y, 58, 3, text);
}

static void errors(void)
{
    unsigned i;
    char text[128];
    if (a.frozen) return;
    for (i = 0; i < HISTORY; ++i) {
        if (i < a.used) {
            unsigned n = (a.next+HISTORY-a.used+i)%HISTORY;
            const mt_error *e = &a.history[n];
            boot_format(text, sizeof(text), "%5u  %2u    %08x     %08x   %08x   %08x",
                   (unsigned)a.history_pass[n], e->test+1, (unsigned)e->address,
                   (unsigned)e->expected, (unsigned)e->actual, (unsigned)(e->expected ^ e->actual));
            field(0, 17+i, 95, 10, text);
        } else field(0, 17+i, 95, 3, "");
    }
    if (!a.used) field(0, 18, 95, 3, "  No errors detected. Testing continues until you press ESC.");
}

static unsigned tested_bytes(void)
{
    unsigned bytes = a.active & MT_ALL_TESTS ? a.ram-HIGH_START : 0;
    return bytes + (a.active & LOW_MASK ? 56u*1024u : 0);
}

static void status(const char *message)
{
    char text[160];
    unsigned seconds = (unsigned)((ticks()-a.start)/100u);
    unsigned tp = percent(), pp = a.count ? (a.index*100u+tp)/a.count : 0;
    if (pp > 100) pp = 100;
    bar(0, "Pass", pp); bar(1, "Test", tp);
    boot_format(text, sizeof(text), "Test #%u [%s]", a.current.test+1, test_name(a.current.test));
    field(37, 2, 58, 3, text);
    boot_format(text, sizeof(text), "Testing: %08x  (%uK)", (unsigned)a.current.address,
           a.current.test == LOW_TEST ? 56u : (unsigned)(a.ram-HIGH_START)/1024u);
    field(37, 3, 58, 3, text);
    boot_format(text, sizeof(text), "Pattern: %08x  Step %u/%u", (unsigned)a.current.pattern,
           a.current.stage+1, a.current.stages);
    field(37, 4, 58, 3, text);
    field(37, 5, 58, 3, a.current.operation ? a.current.operation : "Preparing tests");
    boot_format(text, sizeof(text), "Test RAM : %7uK", tested_bytes()/1024u);
    field(0, 4, 35, 3, text);
    boot_format(text, sizeof(text), "%5u:%02u:%02u  %7uK  %7uK  Direct RAM %-7s %4u %7u %10u",
           seconds/3600u, seconds/60u%60u, seconds%60u, tested_bytes()/1024u,
           (unsigned)(a.ram-tested_bytes())/1024u, a.cache, a.count,
           (unsigned)a.passes, (unsigned)a.ctx.errors);
    field(0, 12, 95, 3, text);
    boot_format(text, sizeof(text), "Low RAM: $00002000-$0000FFFF, 56K, six patterns; cumulative errors: %u",
           (unsigned)a.low_errors);
    field(0, 8, 95, 3, text);
    field(0, 30, 95, a.ctx.errors ? 10 : 3, message);
    errors();
}

static void screen(void)
{
    unsigned i;
    char text[112];
    for (i = 0; i < 32; ++i) field(0, i, 95, 3, "");
    field(0, 0, 35, 9, "        Memtest68 " BOOT_MEDIUM " v0.2");
    boot_format(text, sizeof(text), "Motorola 680%02u / X680x0", a.cpu*10u);
    field(0, 1, 35, 3, text);
    boot_format(text, sizeof(text), "Main RAM : %7uK (SRAM)", (unsigned)a.ram/1024u);
    field(0, 3, 35, 3, text);
    boot_format(text, sizeof(text), "Test RAM : %7uK", tested_bytes()/1024u);
    field(0, 4, 35, 3, text);
    boot_format(text, sizeof(text), "D-Cache  : %s", a.cache);
    field(0, 5, 35, 3, text);
    for (i = 0; i < 7; ++i) field(35, i, 1, 3, "|");
    field(37, 6, 58, 3, "----------------------------------------------------------");
    field(0, 7, 95, 3, BOOT_SOURCE " boot / NO OS. Reserved: first 8K (ROM IOCS vectors/work). Low RAM runs in VRAM.");
    field(0, 10, 95, 3, "  WallTime   Tested    Excluded  MemMap     D-Cache Test    Pass     Errors");
    rule(11); rule(13);
    field(0, 15, 95, 3, " Pass  Test   Failing address  Good       Bad        Err-Bits");
    rule(16); rule(29);
    field(0, 31, 95, 11, "(ESC) " BOOT_EXIT_NAME "  (c) Configuration  (SP) Scroll lock  (CR) Unlock  (p) Pause");
    status("Testing memory. Press c to configure tests for the next pass.");
}

static int key(void)
{
    return _iocs_b_keysns() ? _iocs_b_keyinp() & 255 : 0;
}

static void configure(void)
{
    int k;
    unsigned i;
    char text[112];
    do {
        field(0, 15, 95, 9, "  Configuration - test selection applies to the NEXT pass");
        for (i = 0; i < 8; ++i) {
            boot_format(text, sizeof(text), "  (%u) [%s] %s", i+1, a.mask & (1u << i) ? "X" : " ", test_name(i));
            field(0, 17+i, 95, 3, text);
        }
        if (a.limit) boot_format(text, sizeof(text), "  (l) Pass limit: %u", (unsigned)a.limit);
        else boot_format(text, sizeof(text), "  (l) Pass limit: %s", "unlimited");
        field(0, 25, 95, 3, text);
        field(0, 26, 95, 3, "  1-8 toggle   a select all   l cycle pass limit (1/2/5/unlimited)");
        field(0, 27, 95, 3, "  ENTER resume (at least one test required)   ESC " BOOT_EXIT_LOWER);
        k = _iocs_b_keyinp() & 255;
        if (k >= '1' && k <= '8') a.mask ^= 1u << (k-'1');
        if (k == 'a' || k == 'A') a.mask = ALL_TESTS;
        if (k == 'l' || k == 'L') a.limit = a.limit == 0 ? 1 : a.limit == 1 ? 2 : a.limit == 2 ? 5 : 0;
        if (k == 27) { a.quit = 1; break; }
    } while (k != 13 || !a.mask);
    a.frozen = 0;
    screen();
}

static int progress(void *user, const mt_progress *p)
{
    int k;
    uint32_t now;
    (void)user;
    a.current = *p;
    k = key();
    if (k == 27) a.quit = 1;
    if (k == 'c' || k == 'C') configure();
    if (k == ' ') a.frozen = !a.frozen;
    if (k == 13) a.frozen = 0;
    if (k == 'p' || k == 'P') {
        status("PAUSED - press p to continue, ESC to " BOOT_EXIT_LOWER ".");
        do { k = _iocs_b_keyinp() & 255; } while (k != 'p' && k != 'P' && k != 27);
        if (k == 27) a.quit = 1;
    }
    now = ticks();
    if (now-a.last_draw >= 20 || !p->done || p->done == p->total) {
        a.last_draw = now;
        status(a.frozen ? "SCROLL LOCK - counters continue; ENTER unlocks the error list." :
               a.passes ? "Pass complete. Testing continues; ESC " BOOT_EXIT_VERB "." :
                          "Testing memory. Press c to configure tests for the next pass.");
    }
    return a.quit;
}

static void error(void *user, const mt_error *e)
{
    unsigned n = a.next;
    (void)user;
    a.history[n] = *e;
    a.history_pass[n] = a.passes+1;
    a.next = (n+1)%HISTORY;
    if (a.used < HISTORY) ++a.used;
}

static uint32_t saturated_add(uint32_t x, uint32_t y)
{
    return UINT32_MAX-x < y ? UINT32_MAX : x+y;
}

static void low_ram(void)
{
    volatile unsigned char *dest = (volatile unsigned char *)0xe40000;
    const unsigned char *source = low_probe_image_start;
    uint32_t found;
    mt_progress p = {LOW_TEST, 0, 1, 0, 0, 56u*1024u/4u, 0x2000,
                     "VRAM probe: 0/1, 55/AA, address/inverse"};
    while (source < low_probe_image_end) *dest++ = *source++;
    if (progress(0, &p)) return;
    if (a.cpu == 3) _iocs_cache_md(-1);
    found = ((uint32_t (*)(void))0xe40000)();
    a.low_errors = saturated_add(a.low_errors, found);
    a.ctx.errors = saturated_add(a.ctx.errors, found);
    if (found) {
        volatile uint32_t *info = (volatile uint32_t *)0xe5ff0c;
        mt_error e = {(uintptr_t)info[0], info[1], info[2], LOW_TEST};
        error(0, &e); /* First low-RAM mismatch is retained; all are counted. */
    }
    p.done = p.total;
    progress(0, &p);
}

static void reboot(void)
{
    int k;
#ifdef BOOT_HDS
    status("Restart tester from HDS? Press y to reload, any other key to resume.");
#else
    status("Reboot now? Press y to reboot from " BOOT_MEDIUM ", any other key to resume.");
#endif
    k = _iocs_b_keyinp() & 255;
    if (k == 'y' || k == 'Y') {
#ifdef BOOT_HDS
        boot_restart_hds();
#else
        _iocs_iplerr();
#endif
    }
    a.quit = 0;
}

static void fatal(const char *message)
{
    _iocs_b_color(3);
    _iocs_b_print(message);
    _iocs_b_print("\r\nPress a key to " BOOT_EXIT_LOWER ".\r\n");
    _iocs_b_keyinp();
#ifdef BOOT_HDS
    boot_restart_hds();
#else
    _iocs_iplerr();
#endif
}

void boot_main(void)
{
    mt_region region;
    unsigned i, j;
    a.cpu = (unsigned)_iocs_b_bpeek((const void *)0xcbc) & 255u;
    a.ram = *(volatile uint32_t *)0xed0008;
    a.mask = a.active = ALL_TESTS;
    a.count = 8;
    a.cache = "none";
    if (a.cpu > 3) fatal("Unsupported accelerator: cache control is not implemented.");
    if (a.ram < 0x100000 || a.ram > MAX_RAM || (a.ram & 0xfffff))
        fatal("Invalid SRAM memory-size setting. Configure 1..12 MiB before booting.");
    if (a.cpu == 3) {
        int old = _iocs_cache_st();
        if (old < 0 || _iocs_cache_md(old & 1) < 0 || (_iocs_cache_st() & 2))
            fatal("Cannot disable the 68030 data cache.");
        a.cache = "off";
    }
    _iocs_crtmod(16);
    _iocs_b_consol(0, 0, 95, 31);
    _iocs_b_color(3);
    _iocs_b_clr_al();
    _iocs_tpalet(0, 0x003e); _iocs_tpalet(1, 0xf800);
    _iocs_tpalet(2, 0xffc0); _iocs_tpalet(3, 0xfffe);
    /* Plane 2/3 are live probe code/backup storage. Repeat colors for all
     * upper-plane combinations so these bytes never affect visible pixels.
     * _TPALET groups entries 4..7 and 8..15, so write the hardware palette. */
    for (i = 4; i < 16; ++i) {
        static const uint16_t colors[4] = {0x003e,0xf800,0xffc0,0xfffe};
        ((volatile uint16_t *)0xe82200)[i] = colors[i & 3u];
    }
    if (((unsigned)_iocs_romver() >> 24) >= 0x13) _iocs_os_curof();
    else _iocs_b_curoff();
    for (i = 0; i < 32; ++i) for (j = 0; j < 96; ++j) a.color[i][j] = 255;
    a.start = ticks();
    a.current.stages = 1;
    a.ctx.progress = progress; a.ctx.error = error;
    region.base = (volatile uint32_t *)HIGH_START;
    region.words = (a.ram-HIGH_START)/4;
    screen();
    for (;;) {
        a.active = a.mask;
        a.count = count_bits(a.active);
        a.index = 0;
        if (a.active & LOW_MASK) {
            low_ram();
            if (!a.quit) ++a.index;
        }
        for (i = 0; i < MT_TEST_COUNT && !a.quit; ++i) {
            if (!(a.active & (1u << i))) continue;
            if (mt_run(&a.ctx, &region, 1, i, UINT32_C(0x68c0ffee) ^ a.passes)) a.quit = 1;
            if (!a.quit) ++a.index;
        }
        if (a.quit) { reboot(); continue; }
        if (a.passes != UINT32_MAX) ++a.passes;
        if (a.limit && a.passes >= a.limit) {
            a.index = a.count-1;
            status(a.ctx.errors ? "***** Pass complete - ERRORS DETECTED. ESC " BOOT_EXIT_VERB ", ENTER runs one more pass. *****" :
                                  "***** Pass complete, no errors. ESC " BOOT_EXIT_VERB ", ENTER runs one more pass. *****");
            for (;;) {
                int k = _iocs_b_keyinp() & 255;
                if (k == 27) reboot();
                if (k == 'c' || k == 'C') { configure(); if (a.quit) reboot(); }
                if (k == 13) { if (a.limit) a.limit = a.passes+1; break; }
            }
        }
    }
}
