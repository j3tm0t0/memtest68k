#include "memtest.h"
#if defined(__m68k__) && !defined(MT_PORTABLE)
#include "memtest68k.h"
#endif

const char *const mt_test_names[MT_TEST_COUNT] = {
    "Address, own address", "Moving inversions, 0 & 1",
    "Moving inversions, checkerboard", "Walking bits, distributed",
    "Random pattern & complement", "March C-", "Byte / word access"
};

enum pattern_kind { CONSTANT, ADDRESS, RANDOM, ROTATING };
enum action { FILL, CHECK, INVERT, BYTE_WRITE, WORD_WRITE };

typedef struct {
    mt_context *ctx;
    const mt_region *regions;
    size_t count;
    mt_progress p;
    uint32_t seed;
} runner;

static uint32_t random_value(uint32_t v)
{
    v ^= v << 13;
    v ^= v >> 17;
    v ^= v << 5;
    return v;
}

#if defined(__m68k__) && !defined(MT_PORTABLE)
/* xorshift is linear over XOR. Split the address at 1 KiB boundaries:
 * T(address ^ seed) = T(low ten address bits) ^ T(high bits ^ seed).
 * Only 256 longword-aligned low-bit values are needed. */
uint32_t mt68_random_table[256];
#endif

#if !defined(__m68k__) || defined(MT_PORTABLE)
static uint32_t expected_at(runner *r, uintptr_t address,
                            enum pattern_kind kind, uint32_t pattern)
{
    uint32_t v = (uint32_t)address;
    if (kind == CONSTANT) return pattern;
    if (kind == ADDRESS) return v ^ pattern;
    if (kind == ROTATING) return (UINT32_C(1) << (((v >> 2) + r->seed) & 31u)) ^ pattern;
    /* Deterministic per-address hash; independent of traversal direction. */
    return random_value(v ^ r->seed) ^ pattern;
}

#endif

static void mismatch(runner *r, uintptr_t address, uint32_t want, uint32_t got)
{
    mt_error e;
    if (want == got) return;
    if (r->ctx->errors != UINT32_MAX) ++r->ctx->errors;
    e.address = address;
    e.expected = want;
    e.actual = got;
    e.test = r->p.test;
    if (r->ctx->error) r->ctx->error(r->ctx->user, &e);
}

static int progress(runner *r)
{
    return r->ctx->progress && r->ctx->progress(r->ctx->user, &r->p);
}

static int sweep(runner *r, enum pattern_kind kind, uint32_t pattern,
                 enum action action, int reverse, const char *operation)
{
    size_t block, i;
    r->p.done = 0;
    r->p.pattern = pattern;
    r->p.operation = operation;
    r->p.address = (uintptr_t)r->regions[0].base;
    if (progress(r)) return 1;
    for (block = 0; block < r->count; ++block) {
        const mt_region *region = &r->regions[reverse ? r->count-1-block : block];
        i = 0;
        while (i < region->words) {
            size_t n = 4096u - (r->p.done & 4095u);
            size_t offset = reverse ? region->words-1-i : i;
            volatile uint32_t *p = region->base + offset;
            if (n > region->words-i) n = region->words-i;
#if defined(__m68k__) && !defined(MT_PORTABLE)
            mt68_chunk chunk = {p, (uint32_t)n, pattern, r->seed, 0, 0};
            unsigned mode = action >= BYTE_WRITE ? 24u + (unsigned)(action-BYTE_WRITE) :
                            ((unsigned)kind*3u + (unsigned)action)*2u + (unsigned)reverse;
            /* Keep random chunks within one 1 KiB address window. Progress
             * still fires at exactly the same global 4096-word boundaries. */
            if (kind == RANDOM) {
                size_t in_window = ((uintptr_t)p & 0x3ffu)/4u;
                size_t available = reverse ? in_window+1u : 256u-in_window;
                if (n > available) n = available;
                chunk.words = (uint32_t)n;
                chunk.seed = random_value(((uint32_t)(uintptr_t)p & ~UINT32_C(0x3ff)) ^ r->seed) ^ pattern;
            }
            /* BYTE_WRITE=3 -> 24; WORD_WRITE=4 -> 25. */
            if (kind == ROTATING)
                chunk.pattern = (UINT32_C(1) << ((((uint32_t)(uintptr_t)p >> 2) + r->seed) & 31u)) ^ pattern;
            while (chunk.words) {
                uint32_t remaining = mt68_sweep(&chunk, mode);
                if (!remaining) break;
                mismatch(r, (uintptr_t)chunk.cursor, chunk.expected, chunk.actual);
                if (action == INVERT) *chunk.cursor = ~chunk.expected;
                chunk.words = remaining-1u;
                if (chunk.words) chunk.cursor += reverse ? -1 : 1;
                if (kind == ROTATING && chunk.words)
                    chunk.pattern = (UINT32_C(1) << ((((uint32_t)(uintptr_t)chunk.cursor >> 2) + r->seed) & 31u)) ^ pattern;
            }
#else
            size_t word;
            for (word = 0; word < n; ++word) {
                volatile uint32_t *at = reverse ? p-word : p+word;
                uint32_t want = expected_at(r, (uintptr_t)at, kind, pattern);
                if (action == FILL) *at = want;
                else if (action == BYTE_WRITE) {
                    const unsigned char *bytes = (const unsigned char *)&want;
                    volatile unsigned char *dest = (volatile unsigned char *)at;
                    unsigned lane;
                    for (lane = 0; lane < 4; ++lane) dest[lane] = bytes[lane];
                } else if (action == WORD_WRITE) {
                    uint16_t hi = (uint16_t)(want >> 16), lo = (uint16_t)want;
                    volatile uint16_t *dest = (volatile uint16_t *)at;
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
                    dest[0] = hi; dest[1] = lo;
#else
                    dest[0] = lo; dest[1] = hi;
#endif
                } else {
                    uint32_t got = *at;
                    mismatch(r, (uintptr_t)at, want, got);
                    if (action == INVERT) *at = ~want;
                }
            }
#endif
            i += n;
            r->p.done += n;
            if ((r->p.done & 4095u) == 0) {
                r->p.address = (uintptr_t)(reverse ? p-(n-1) : p+(n-1));
                if (progress(r)) return 1;
            }
        }
    }
    r->p.address = (uintptr_t)r->regions[r->count-1].base;
    if (progress(r)) return 1;
    ++r->p.stage;
    return 0;
}

static int inversions(runner *r, enum pattern_kind kind, uint32_t pattern)
{
    return sweep(r, kind, pattern, FILL, 0, "write ascending") ||
           sweep(r, kind, pattern, INVERT, 0, "read / invert ascending") ||
           sweep(r, kind, ~pattern, INVERT, 1, "read / invert descending") ||
           sweep(r, kind, pattern, CHECK, 0, "verify ascending");
}

int mt_run_ex(mt_context *ctx, const mt_region *regions, size_t count,
              unsigned test, uint32_t seed, unsigned flags)
{
    runner r = {0};
    size_t i, j;
    unsigned bit;
    if (!ctx || !regions || !count || count > MT_MAX_REGIONS || test >= MT_TEST_COUNT)
        return -1;
    r.ctx = ctx; r.regions = regions; r.count = count; r.seed = seed;
    r.p.test = test;
#if defined(__m68k__) && !defined(MT_PORTABLE)
    if (test == 4)
        for (i = 0; i < 256; ++i) mt68_random_table[i] = random_value((uint32_t)i*4u);
#endif
    for (i = 0; i < count; ++i) {
        uintptr_t base = (uintptr_t)regions[i].base;
        size_t words = regions[i].words;
        if (!base || (base & 3) || !words || words > (UINTPTR_MAX-base)/4 ||
            words > SIZE_MAX-r.p.total) return -1;
        for (j = 0; j < i; ++j) {
            uintptr_t other = (uintptr_t)regions[j].base;
            if (base < other + regions[j].words*4 && other < base + words*4)
                return -1;
        }
        r.p.total += words;
    }
    switch (test) {
    case 0:
        r.p.stages = 4;
        return sweep(&r, ADDRESS, 0, FILL, 0, "write own address") ||
               sweep(&r, ADDRESS, 0, CHECK, 1, "verify own address") ||
               sweep(&r, ADDRESS, UINT32_MAX, FILL, 0, "write complemented address") ||
               sweep(&r, ADDRESS, UINT32_MAX, CHECK, 1, "verify complemented address");
    case 1:
    case 2:
    case 4:
        r.p.stages = 8;
        if (test == 4) return inversions(&r, RANDOM, 0) || inversions(&r, RANDOM, UINT32_MAX);
        if (test == 2) return inversions(&r, CONSTANT, UINT32_C(0x55555555)) ||
                              inversions(&r, CONSTANT, UINT32_C(0xaaaaaaaa));
        return inversions(&r, CONSTANT, 0) || inversions(&r, CONSTANT, UINT32_MAX);
    case 3:
        if (!(flags & MT_WALK_FULL)) {
            r.p.stages = 4;
            return inversions(&r, ROTATING, 0);
        }
        r.p.stages = 128;
        for (bit = 0; bit < 32; ++bit)
            if (inversions(&r, CONSTANT, UINT32_C(1) << bit)) return 1;
        return 0;
    case 5:
        r.p.stages = 6;
        return sweep(&r, CONSTANT, 0, FILL, 0, "March: write 0") ||
               sweep(&r, CONSTANT, 0, INVERT, 0, "March: up r0 w1") ||
               sweep(&r, CONSTANT, UINT32_MAX, INVERT, 0, "March: up r1 w0") ||
               sweep(&r, CONSTANT, 0, INVERT, 1, "March: down r0 w1") ||
               sweep(&r, CONSTANT, UINT32_MAX, INVERT, 1, "March: down r1 w0") ||
               sweep(&r, CONSTANT, 0, CHECK, 0, "March: read 0");
    case 6:
        r.p.stages = 4;
        return sweep(&r, CONSTANT, UINT32_C(0x01234567), BYTE_WRITE, 0, "write byte lanes") ||
               sweep(&r, CONSTANT, UINT32_C(0x01234567), CHECK, 1, "verify byte lanes") ||
               sweep(&r, CONSTANT, UINT32_C(0x89abcdef), WORD_WRITE, 0, "write word lanes") ||
               sweep(&r, CONSTANT, UINT32_C(0x89abcdef), CHECK, 1, "verify word lanes");
    }
    return -1;
}

int mt_run(mt_context *ctx, const mt_region *regions, size_t count,
           unsigned test, uint32_t seed)
{
    return mt_run_ex(ctx, regions, count, test, seed, 0);
}
