#include "probe.h"
/* Copied to TVRAM plane 2. No IOCS or DRAM-resident code/data while testing.
 * uint32_t probe(unsigned cpu): cpu=3 tests $0..$ffff with VBR relocated;
 * other CPUs test $400..$ffff, keeping their fixed 1 KiB vector table.
 * The complete original 64 KiB, including vectors and caller stack, is saved
 * in plane 3 and restored before returning. DMA must be idle; its channels
 * are halted for the probe. Exceptions use our own VRAM recovery handler.
 */
    .section .text
    .global low_probe_image_start,low_probe_image_end
low_probe_image_start:
    movem.l %d2-%d7/%a2-%a6,-(%sp)
    move.w %sr,%d0
    move.w #0x2700,%sr
    move.w 0xe8002a,%d1
    clr.w 0xe8002a
    move.l %sp,LP_SP
    move.w %d0,LP_SR
    move.w %d1,LP_CRTC
    move.l 48(%sp),LP_CPU
    lea LP_STACK,%sp
    clr.l LP_STATUS
    clr.l LP_STATE
    clr.l LP_ERRORS
    clr.l LP_RESTORE_ERRORS
    clr.l LP_OLD_VBR
    clr.l LP_ACTIVE_VBR
    clr.l LP_RESTORED_VBR
    /* Do not overwrite RAM if DMA is still using it. */
    lea 0xe84000,%a0
    moveq #3,%d2
dma_check:
    btst #3,(%a0)
    bne dma_busy
    lea 0x40(%a0),%a0
    dbra %d2,dma_check
    lea 0xe84007,%a0
    lea LP_DMA_CCR,%a1
    moveq #3,%d2
dma_halt:
    move.b (%a0),%d0
    move.b %d0,(%a1)+
    and.b #8,%d0           /* Preserve ITE; do not replay STR/CNT commands. */
    or.b #0x20,%d0
    move.b %d0,(%a0)
    lea 0x40(%a0),%a0
    dbra %d2,dma_halt
    suba.l %a0,%a0
    lea LP_BACKUP,%a1
    move.w #16383,%d2
backup:
    move.l (%a0)+,(%a1)+
    dbra %d2,backup
    lea exception(%pc),%a2
    moveq #0,%d5
    cmp.l #3,LP_CPU
    bne fixed_vectors
    /* MOVEC VBR,D0 / D0,VBR, executed ONLY on the 68030 branch.
     * Encoding keeps the whole object assemblable with -m68000. */
    .word 0x4e7a,0x0801
    move.l %d0,LP_OLD_VBR
    lea LP_VECTORS,%a0
    move.w #255,%d2
vbr_vectors:
    move.l %a2,(%a0)+
    dbra %d2,vbr_vectors
    move.l #LP_VECTORS,%d0
    .word 0x4e7b,0x0801
    .word 0x4e7a,0x0801
    move.l %d0,LP_ACTIVE_VBR
    suba.l %a4,%a4
    move.w #16383,%d7
    bra ready
fixed_vectors:
    /* Leave initial reset SP/PC alone; replace exception vectors temporarily.
     * They are not tested and are restored from the full backup. */
    lea 8,%a0
    move.w #253,%d2
fixed_fill:
    move.l %a2,(%a0)+
    dbra %d2,fixed_fill
    lea 0x400,%a4
    move.w #16127,%d7
ready:
    move.l %a4,LP_START
    move.l #LP_RUNNING,LP_STATE
    moveq #0,%d6
pattern:
    movea.l %a4,%a0
    moveq #0,%d3
    tst.w %d6
    beq fill
    moveq #-1,%d3
    cmp.w #1,%d6
    beq fill
    move.l #0x55555555,%d3
    cmp.w #2,%d6
    beq fill
    move.l #0xaaaaaaaa,%d3
fill:
    move.w %d7,%d2
fill_word:
    cmp.w #4,%d6
    blt fill_constant
    move.l %a0,%d3
    cmp.w #5,%d6
    bne fill_constant
    not.l %d3
fill_constant:
    move.l %d3,(%a0)+
    dbra %d2,fill_word
    movea.l %a4,%a0
    move.w %d7,%d2
verify:
    cmp.w #4,%d6
    blt read_word
    move.l %a0,%d3
    cmp.w #5,%d6
    bne read_word
    not.l %d3
read_word:
    move.l (%a0),%d4
    cmp.l %d3,%d4
    beq good
    tst.l %d5
    bne counted
    move.l %a0,LP_FIRST_ADDRESS
    move.l %d3,LP_FIRST_EXPECTED
    move.l %d4,LP_FIRST_ACTUAL
counted:
    addq.l #1,%d5
good:
    addq.l #4,%a0
    dbra %d2,verify
    addq.w #1,%d6
    cmp.w #6,%d6
    bne pattern
    bra restore
exception:
    /* Discard the exception frame on the VRAM stack. Never call ROM here. */
    move.w #0x2700,%sr
    lea LP_STACK,%sp
    move.l #LP_EXCEPTION,LP_STATUS
restore:
    move.l %d5,LP_ERRORS
    lea LP_BACKUP,%a0
    suba.l %a1,%a1
    move.w #16383,%d2
restore_word:
    move.l (%a0)+,(%a1)+
    dbra %d2,restore_word
    /* Compare the entire restored DRAM image BEFORE the caller can mutate it. */
    lea LP_BACKUP,%a0
    suba.l %a1,%a1
    move.w #16383,%d2
restore_verify:
    cmpm.l (%a0)+,(%a1)+
    beq restored_word
    addq.l #1,LP_RESTORE_ERRORS
    move.l #LP_RESTORE_FAILED,LP_STATUS
restored_word:
    dbra %d2,restore_verify
    cmp.l #3,LP_CPU
    bne dma_restore
    move.l LP_OLD_VBR,%d0
    .word 0x4e7b,0x0801
    .word 0x4e7a,0x0801
    move.l %d0,LP_RESTORED_VBR
dma_restore:
    lea 0xe84007,%a0
    lea LP_DMA_CCR,%a1
    moveq #3,%d2
dma_resume:
    move.b (%a1)+,%d0
    and.b #0x28,%d0
    move.b %d0,(%a0)
    lea 0x40(%a0),%a0
    dbra %d2,dma_resume
    move.l #LP_COMPLETE,LP_STATE
    bra finish
dma_busy:
    move.l #LP_DMA_ACTIVE,LP_STATUS
finish:
    move.l LP_ERRORS,%d0
    move.w LP_CRTC,0xe8002a
    movea.l LP_SP,%sp
    movem.l (%sp)+,%d2-%d7/%a2-%a6
    move.w LP_SR,%sr
    rts
low_probe_image_end:
