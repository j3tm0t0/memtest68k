#include "memtest.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    uint32_t guard1[4], data[8193], guard2[4];
} guarded;

static guarded buffer;
static uint32_t second[17];
static unsigned reports, final_stage, progress_calls;
static int inject, cancel;
static mt_error last_error;
static size_t last_done;
static unsigned last_stage;
static uint32_t flipped;

static int observe(void *user, const mt_progress *p)
{
    (void)user;
    assert(p->done <= p->total);
    assert(p->stage < p->stages);
    if (p->stage == last_stage) assert(p->done >= last_done);
    last_stage = p->stage;
    last_done = p->done;
    ++progress_calls;
    final_stage = p->stage;
    if (inject && p->stage == 1 && p->done == 0) {
        buffer.data[123] ^= UINT32_C(0x10000001);
        flipped = buffer.data[123];
    }
    return cancel && progress_calls == 3;
}

static void report(void *user, const mt_error *e)
{
    (void)user;
    ++reports;
    last_error = *e;
}

static void reset(void)
{
    unsigned i;
    memset(&buffer, 0x37, sizeof(buffer));
    for (i = 0; i < 4; ++i) {
        buffer.guard1[i] = UINT32_C(0xdeadbeef);
        buffer.guard2[i] = UINT32_C(0xcafef00d);
    }
    reports = progress_calls = final_stage = 0;
    last_stage = 0; last_done = 0;
    inject = cancel = 0;
}

static void guards(void)
{
    unsigned i;
    for (i = 0; i < 4; ++i) {
        assert(buffer.guard1[i] == UINT32_C(0xdeadbeef));
        assert(buffer.guard2[i] == UINT32_C(0xcafef00d));
    }
}

int main(void)
{
    static const unsigned stages[] = {4,8,8,128,8,6,4};
    mt_region regions[] = {{buffer.data,8193}, {second,17}};
    mt_context ctx = {observe,report,NULL,0};
    unsigned test;
    for (test = 0; test < MT_TEST_COUNT; ++test) {
        reset(); ctx.errors = 0;
        assert(mt_run(&ctx, regions, 2, test, 0x68) == 0);
        assert(ctx.errors == 0 && reports == 0);
        assert(final_stage+1 == stages[test]);
        assert(progress_calls > stages[test]*2); /* Chunk callbacks occur too. */
        guards();

        reset(); ctx.errors = 0; inject = 1;
        assert(mt_run(&ctx, regions, 2, test, 0x68) == 0);
        assert(ctx.errors > 0 && reports == ctx.errors);
        assert(last_error.address == (uintptr_t)&buffer.data[123]);
        assert(last_error.test == test);
        assert(last_error.actual == flipped);
        assert((last_error.actual ^ last_error.expected) == UINT32_C(0x10000001));
        guards();

        reset(); ctx.errors = 0; cancel = 1;
        assert(mt_run(&ctx, regions, 2, test, 0x68) == 1);
        assert(progress_calls == 3);
        guards();
    }
    /* Counter saturation, rather than wrapping to a false clean result. */
    reset(); ctx.errors = UINT32_MAX; inject = 1;
    assert(mt_run(&ctx, regions, 1, 0, 42) == 0);
    assert(ctx.errors == UINT32_MAX && reports == 1);

    /* One-word regions and invalid ranges, including overlap and misalignment. */
    reset(); ctx.errors = 0;
    regions[0].words = 1;
    assert(mt_run(&ctx, regions, 1, 5, 42) == 0);
    assert(mt_run(NULL, regions, 1, 0, 0) == -1);
    assert(mt_run(&ctx, NULL, 1, 0, 0) == -1);
    assert(mt_run(&ctx, regions, 0, 0, 0) == -1);
    assert(mt_run(&ctx, regions, 1, MT_TEST_COUNT, 0) == -1);
    regions[1] = regions[0];
    assert(mt_run(&ctx, regions, 2, 0, 0) == -1);
    regions[0].base = (volatile uint32_t *)((unsigned char *)buffer.data+1);
    assert(mt_run(&ctx, regions, 1, 0, 0) == -1);
    regions[0].base = buffer.data; regions[0].words = SIZE_MAX;
    assert(mt_run(&ctx, regions, 1, 0, 0) == -1);
    puts("All 7 tests: clean RAM, fault reporting, cancellation, guards and bounds PASS");
    return 0;
}
