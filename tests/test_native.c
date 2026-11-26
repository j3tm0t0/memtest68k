/* Run the actual 68000 kernels against an independently compiled portable
 * engine at identical addresses. Build as Human68k X and execute with run68x. */
#include "memtest.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int ref_mt_run_ex(mt_context *, const mt_region *, size_t, unsigned, uint32_t, unsigned);
static uint32_t memory[8195], tail[19], snapshot[8195], tail_snapshot[19];
static size_t words;
static unsigned scenario, calls, errors;
static uint32_t trace;
static void hash(uint32_t value) { trace = (trace ^ value) * UINT32_C(16777619); }

static int progress(void *user, const mt_progress *p)
{
    size_t i;
    const char *s;
    (void)user;
    ++calls;
    hash(p->test); hash(p->stage); hash(p->stages); hash(p->pattern);
    hash(p->done); hash(p->total); hash((uint32_t)p->address);
    for (s = p->operation; *s; ++s) hash((unsigned char)*s);
    /* Compare intermediate RAM contents, not just the final restored pattern. */
    for (i = 0; i < words+2; ++i) hash(memory[i]);
    for (i = 0; i < 19; ++i) hash(tail[i]);
    if (scenario == 1 && p->stage == 1 && p->done == 0) {
        memory[1] ^= 1;
        if (words > 1) memory[words] ^= UINT32_C(0x80000000);
        if (words > 4096) { memory[4096] ^= 0x123; memory[4097] ^= 0x456; }
        tail[1] ^= 0x80;
    }
    return scenario == 2 && calls == 3;
}
static void error(void *user, const mt_error *e)
{
    (void)user;
    ++errors;
    hash((uint32_t)e->address); hash(e->expected); hash(e->actual); hash(e->test);
    /* Mutate the following word from the first callback. Invert must still
     * store AFTER the callback, and resume without skipping a second fault. */
    if (scenario == 1 && errors == 1 && words > 2) memory[2] ^= 0x42;
}
static void reset(void)
{
    memset(memory, 0x37, sizeof(memory)); memset(tail, 0xa5, sizeof(tail));
    trace = UINT32_C(2166136261); calls = errors = 0;
}
int main(void)
{
    static const size_t lengths[] = {1, 31, 4095, 4096, 4097, 8193};
    static const uint32_t seeds[] = {0, UINT32_MAX, UINT32_C(0x80000000), 1, UINT32_C(0x12345678), UINT32_C(0x68c0ffee)};
    unsigned n, test, flags;
    for (n = 0; n < sizeof(lengths)/sizeof(lengths[0]); ++n) {
        mt_region regions[] = {{memory+1,lengths[n]}, {tail+1,17}};
        words = lengths[n];
        printf("Differential: %lu words + 17-word region\n", (unsigned long)words);
        fflush(stdout);
        for (test = 0; test < MT_TEST_COUNT; ++test)
            for (flags = 0; flags <= MT_WALK_FULL; ++flags) {
                if (flags && test != 3) continue;
                for (scenario = 0; scenario < 3; ++scenario) {
                    mt_context ctx = {progress,error,NULL,scenario == 1 ? UINT32_MAX-1u : 0};
                    int native_result, reference_result;
                    uint32_t native_trace, native_errors;
                    unsigned native_calls, native_reports;
                    reset();
                    native_result = mt_run_ex(&ctx,regions,2,test,seeds[n],flags);
                    native_trace = trace; native_errors = ctx.errors;
                    native_calls = calls; native_reports = errors;
                    memcpy(snapshot,memory,sizeof(memory));
                    memcpy(tail_snapshot,tail,sizeof(tail));
                    reset(); ctx.errors = scenario == 1 ? UINT32_MAX-1u : 0;
                    reference_result = ref_mt_run_ex(&ctx,regions,2,test,seeds[n],flags);
                    if (native_result != reference_result || native_trace != trace ||
                        native_errors != ctx.errors || native_calls != calls || native_reports != errors ||
                        memcmp(snapshot,memory,sizeof(memory)) || memcmp(tail_snapshot,tail,sizeof(tail))) {
                        printf("FAIL: n=%lu test=%u flags=%u scenario=%u trace=%08lx/%08lx reports=%u/%u\n",
                               (unsigned long)words,test,flags,scenario,
                               (unsigned long)native_trace,(unsigned long)trace,native_reports,errors);
                        return 1;
                    }
                    assert(memory[0] == 0x37373737 && memory[words+1] == 0x37373737);
                    assert(tail[0] == 0xa5a5a5a5 && tail[18] == 0xa5a5a5a5);
                }
            }
    }
    puts("68000 ASM/portable differential: 144 cases PASS (patterns, direction, callbacks, faults, saturation, cancellation, chunk edges)");
    return 0;
}
