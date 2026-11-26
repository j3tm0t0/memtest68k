#ifndef MEMTEST68K_H
#define MEMTEST68K_H
#include <stdint.h>
#include <stddef.h>
/* Private, fixed 32-bit ABI shared with memtest68k.S. A chunk has 1..4096
 * words. On a mismatch cursor/expected/actual describe the failing word;
 * the return value counts that word and the unprocessed remainder. */
typedef struct {
    volatile uint32_t *cursor;
    uint32_t words, pattern, seed, expected, actual;
} mt68_chunk;
typedef char mt68_abi_check[(sizeof(mt68_chunk) == 24 &&
    offsetof(mt68_chunk, expected) == 16 && offsetof(mt68_chunk, actual) == 20) ? 1 : -1];
uint32_t mt68_sweep(mt68_chunk *chunk, unsigned mode);
#endif
