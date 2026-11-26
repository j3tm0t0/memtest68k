/* First sector, entered by IPL ROM at $2000. No Human68k code or filesystem.
 * Metadata at $28/$2c is filled by tools/make_fd.py. */
    .section .text
    .global _ipl
_ipl:
    bra.w boot_start
    .ascii "Memtest68 FD IPL"
    .org 0x20
    .ascii "MT68FD1\0"
payload_size:
    .long 0
payload_sum:
    .long 0
    .org 0x40
boot_start:
    move.w #0x2700,%sr
    lea 0x10000,%sp
    move.w #0x2000,%sr
    lea loading(%pc),%a1
    moveq #0x21,%d0
    trap #15
    move.l payload_size(%pc),%d6
    beq invalid
    cmp.l #0x9c00,%d6
    bhi invalid
    move.l %d6,%d0
    and.l #1023,%d0
    bne invalid
    lsr.l #8,%d6
    lsr.l #2,%d6
    moveq #-114,%d0           /* IOCS _BOOTINF; low two bits = FD drive */
    trap #15
    and.l #3,%d0
    lsl.w #8,%d0
    or.w #0x9070,%d0
    move.w %d0,%d7
    move.l #0x03000002,%d5    /* N=3, cylinder=0, head=0, sector=2 */
    lea 0x2400,%a3
next_sector:
    moveq #2,%d4
read_retry:
    move.w %d7,%d1
    move.l %d5,%d2
    move.l #1024,%d3
    movea.l %a3,%a1
    moveq #0x46,%d0          /* IOCS _B_READ */
    trap #15
    tst.l %d0
    bpl read_ok
    dbra %d4,read_retry
    lea read_error(%pc),%a1
    bra failed
read_ok:
    adda.l #1024,%a3
    addq.b #1,%d5
    cmp.b #9,%d5
    bne sector_ready
    move.b #1,%d5
    eor.l #0x100,%d5
    btst #8,%d5
    bne sector_ready
    add.l #0x10000,%d5
sector_ready:
    subq.l #1,%d6
    bne next_sector
    lea 0x2400,%a0
    move.l payload_size(%pc),%d1
    lsr.l #2,%d1
    moveq #0,%d0
checksum:
    add.l (%a0)+,%d0
    subq.l #1,%d1
    bne checksum
    cmp.l payload_sum(%pc),%d0
    bne bad_sum
    jmp 0x2400
bad_sum:
    lea checksum_error(%pc),%a1
    bra failed
invalid:
    lea invalid_error(%pc),%a1
failed:
    moveq #0x21,%d0
    trap #15
    moveq #0,%d0
    trap #15
    moveq #-2,%d0           /* IOCS _IPLERR: restart boot, with no OS installed */
    trap #15
    bra failed
loading:
    .asciz "Memtest68 FD: loading standalone tester...\r\n"
read_error:
    .asciz "FD READ ERROR. Press a key to reboot.\r\n"
checksum_error:
    .asciz "PAYLOAD CHECKSUM ERROR. Press a key to reboot.\r\n"
invalid_error:
    .asciz "INVALID BOOT IMAGE. Press a key to reboot.\r\n"
    .org 1024
