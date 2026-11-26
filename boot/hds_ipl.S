/* SCSI disk IPL, loaded from disk byte $400 to RAM $2000 by SCSI ROM.
 * D4.b on entry is the boot target ID. Metadata at $28/$2c is generated.
 * Uses 512-byte READ(6) through the boot adapter's installed SCSI IOCS. */
    .section .text
    .global _ipl
_ipl:
    bra.w boot_start
    .ascii "Memtest68 HDS IPL"
    .org 0x20
    .ascii "MT68HD1\0"
payload_size:
    .long 0
payload_sum:
    .long 0
boot_target:
    .long 0                 /* Runtime-only target at RAM $2030. */
    .org 0x40
boot_start:
    move.l %d4,%d7
    and.l #7,%d7
    move.l %d7,0x2030
    move.w #0x2700,%sr
    lea 0x10000,%sp
    move.w #0x2000,%sr
    lea loading(%pc),%a1
    moveq #0x21,%d0
    trap #15
    move.l payload_size(%pc),%d0
    beq invalid
    cmp.l #0x9c00,%d0
    bhi invalid
    and.l #511,%d0
    bne invalid
    moveq #2,%d6
read_retry:
    moveq #64,%d2           /* Payload at disk byte $8000. */
    move.l payload_size(%pc),%d3
    lsr.l #8,%d3
    lsr.l #1,%d3
    move.l %d7,%d4
    moveq #1,%d5            /* SCSI IOCS size code 1 = 512-byte blocks. */
    lea 0x2400,%a1
    moveq #0x21,%d1         /* _S_READ */
    moveq #-11,%d0          /* _SCSIDRV */
    trap #15
    tst.l %d0
    bpl read_ok
    dbra %d6,read_retry
    lea read_error(%pc),%a1
    bra failed
read_ok:
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
    move.l boot_target(%pc),%d4
    bra boot_start
loading:
    .asciz "Memtest68 HDS: loading standalone tester...\r\n"
read_error:
    .asciz "SCSI READ ERROR. Press a key to retry.\r\n"
checksum_error:
    .asciz "PAYLOAD CHECKSUM ERROR. Press a key to retry.\r\n"
invalid_error:
    .asciz "INVALID HDS BOOT IMAGE. Press a key to retry.\r\n"
    .org 1024
