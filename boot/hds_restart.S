/* Our IPL and its runtime boot ID survive the low-RAM backup/restore.
 * Reload the tester using the boot adapter's still-installed SCSI IOCS.
 * No ROM _IPLERR: that path does not preserve SCSI boot context. */
    .section .text
    .global boot_restart_hds
boot_restart_hds:
    move.w #0x2700,%sr
    move.l 0x2030,%d4
    jmp 0x2000
