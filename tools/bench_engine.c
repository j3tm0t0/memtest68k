/* Standalone kernel benchmark: no progress drawing or keyboard polling in
 * the timed section. Run on XEiJ XVI at 16 MHz. Results are IOCS centiseconds. */
#include "memtest.h"
#include <x68k/iocs.h>
volatile struct {
    uint32_t magic, state, bytes, modes, errors, ticks[14];
} bench_report;
static uint32_t ticks(void)
{
    struct iocs_time t = _iocs_ontime();
    return (uint32_t)t.day*8640000u + (uint32_t)t.sec;
}
void boot_main(void)
{
    mt_context ctx = {0};
    mt_region region = {(volatile uint32_t *)0x10000,16384};
    unsigned mode, test;
    bench_report.bytes = 65536;
#ifdef MT_BENCH_BASELINE
    bench_report.modes = 1;
#else
    bench_report.modes = 2;
#endif
    bench_report.state = 1;
    bench_report.magic = UINT32_C(0x424d3638);
    for (mode = 0; mode < bench_report.modes; ++mode)
        for (test = 0; test < MT_TEST_COUNT; ++test) {
            uint32_t start = ticks();
#ifdef MT_BENCH_BASELINE
            int result = mt_run(&ctx,&region,1,test,0x68c0ffee);
#else
            int result = mt_run_ex(&ctx,&region,1,test,0x68c0ffee,mode ? MT_WALK_FULL : 0);
#endif
            bench_report.ticks[mode*7+test] = ticks()-start;
            if (result || ctx.errors) { bench_report.errors = ctx.errors+1; bench_report.state = 3; return; }
        }
    bench_report.state = 2;
}
