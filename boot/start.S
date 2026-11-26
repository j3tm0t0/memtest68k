    .section .entry,"ax"
    .global _boot_entry
_boot_entry:
    move.w #0x2700,%sr
    lea 0x10000,%sp
    lea __bss_start,%a0
    lea __bss_end,%a1
1:
    cmpa.l %a1,%a0
    bcc 2f
    clr.l (%a0)+
    bra 1b
2:
    move.w #0x2000,%sr
    jsr boot_main
3:
    bra 3b
