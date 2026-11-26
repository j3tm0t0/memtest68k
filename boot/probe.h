#ifndef BOOT_PROBE_H
#define BOOT_PROBE_H

/* Shared with the position-independent assembler and the headless harness. */
#define LP_CODE 0x00e40000
#define LP_VECTORS 0x00e41000
#define LP_STACK 0x00e5fef0
#define LP_META 0x00e5ff00
#define LP_BACKUP 0x00e60000
#define LP_SP 0x00e5ff00
#define LP_SR 0x00e5ff04
#define LP_CRTC 0x00e5ff06
#define LP_ERRORS 0x00e5ff08
#define LP_FIRST_ADDRESS 0x00e5ff0c
#define LP_FIRST_EXPECTED 0x00e5ff10
#define LP_FIRST_ACTUAL 0x00e5ff14
#define LP_START 0x00e5ff18
#define LP_CPU 0x00e5ff1c
#define LP_OLD_VBR 0x00e5ff20
#define LP_STATUS 0x00e5ff24
#define LP_STATE 0x00e5ff28
#define LP_ACTIVE_VBR 0x00e5ff2c
#define LP_RESTORED_VBR 0x00e5ff30
#define LP_RESTORE_ERRORS 0x00e5ff34
#define LP_DMA_CCR 0x00e5ff38

#define LP_OK 0
#define LP_EXCEPTION 1
#define LP_DMA_ACTIVE 2
#define LP_RESTORE_FAILED 3
#define LP_RUNNING 1
#define LP_COMPLETE 2

#ifndef __ASSEMBLER__
#include <stdint.h>
static inline uint32_t probe_start(unsigned cpu)
{
    return cpu == 3 ? 0u : 0x400u;
}
static inline uint32_t probe_bytes(unsigned cpu)
{
    return 0x10000u - probe_start(cpu);
}
#endif
#endif
