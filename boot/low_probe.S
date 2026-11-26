/* This entire position-independent routine is copied to TVRAM plane 2.
 * IRQs stay masked while RAM $2000..$ffff, including our caller and its
 * stack, is backed up to plane 3, tested, and restored. No IOCS call or
 * reference to a DRAM-resident instruction/data is allowed in this block.
 * TVRAM $e5ff00: saved SP, SR, CRTC R21, count, first addr/expected/actual.
 */
    .section .text
    .global low_probe_image_start,low_probe_image_end
low_probe_image_start:
    movem.l %d2-%d7/%a2-%a6,-(%sp)
    move.w %sr,%d0
    move.w #0x2700,%sr
    move.w 0xe8002a,%d1
    clr.w 0xe8002a           /* Normal, unmasked TVRAM access. */
    move.l %sp,0xe5ff00
    move.w %d0,0xe5ff04
    move.w %d1,0xe5ff06
    lea 0xe5fef0,%sp
    lea 0x2000,%a0
    lea 0xe60000,%a1
    move.w #14335,%d2       /* 56 KiB / 4 - 1 */
1:
    move.l (%a0)+,(%a1)+
    dbra %d2,1b
    moveq #0,%d5            /* Error count. */
    moveq #0,%d6            /* Pattern number: 0,1,55,aa,address,~address. */
pattern:
    lea 0x2000,%a0
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
    move.w #14335,%d2
2:
    cmp.w #4,%d6
    blt 3f
    move.l %a0,%d3
    cmp.w #5,%d6
    bne 3f
    not.l %d3
3:
    move.l %d3,(%a0)+
    dbra %d2,2b
    lea 0x2000,%a0
    move.w #14335,%d2
verify:
    cmp.w #4,%d6
    blt 4f
    move.l %a0,%d3
    cmp.w #5,%d6
    bne 4f
    not.l %d3
4:
    move.l (%a0),%d4
    cmp.l %d3,%d4
    beq good
    tst.l %d5
    bne counted
    move.l %a0,0xe5ff0c
    move.l %d3,0xe5ff10
    move.l %d4,0xe5ff14
counted:
    addq.l #1,%d5
good:
    addq.l #4,%a0
    dbra %d2,verify
    addq.w #1,%d6
    cmp.w #6,%d6
    bne pattern
    lea 0xe60000,%a0
    lea 0x2000,%a1
    move.w #14335,%d2
restore:
    move.l (%a0)+,(%a1)+
    dbra %d2,restore
    move.l %d5,0xe5ff08
    move.l %d5,%d0
    move.w 0xe5ff06,0xe8002a
    movea.l 0xe5ff00,%sp
    movem.l (%sp)+,%d2-%d7/%a2-%a6
    move.w 0xe5ff04,%sr
    rts
low_probe_image_end:
