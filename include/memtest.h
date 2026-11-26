#ifndef MEMTEST_H
#define MEMTEST_H

#include <stddef.h>
#include <stdint.h>

#define MT_TEST_COUNT 7
#define MT_ALL_TESTS ((1u << MT_TEST_COUNT) - 1u)
#define MT_MAX_REGIONS 64

typedef struct {
    volatile uint32_t *base;
    size_t words;
} mt_region;

typedef struct {
    uintptr_t address;
    uint32_t expected, actual;
    unsigned test;
} mt_error;

typedef struct {
    unsigned test, stage, stages;
    uint32_t pattern;
    size_t done, total;
    uintptr_t address;
    const char *operation;
} mt_progress;

typedef struct {
    /* Return nonzero to stop. Called at stage boundaries and every 4096 words. */
    int (*progress)(void *user, const mt_progress *progress);
    void (*error)(void *user, const mt_error *error);
    void *user;
    uint32_t errors; /* Saturates at UINT32_MAX. */
} mt_context;

extern const char *const mt_test_names[MT_TEST_COUNT];
/* Each test spans ALL regions before starting its next stage (alias detection).
 * Regions must be disjoint, uint32_t-aligned, and contain at least one word.
 * Returns 0 on completion, 1 on cancellation, -1 for invalid input. */
int mt_run(mt_context *ctx, const mt_region *regions, size_t count,
           unsigned test, uint32_t seed);

#endif
