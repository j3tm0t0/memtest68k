OUTPUT_FORMAT("elf32-m68k")
OUTPUT_ARCH(m68k)
ENTRY(_boot_entry)
PHDRS
{
    code PT_LOAD FLAGS(5);
    data PT_LOAD FLAGS(6);
}
SECTIONS
{
    . = 0x2400;
    .text : { KEEP(*(.entry)) *(.text .text.*) *(.rodata .rodata.*) } :code
    . = ALIGN(4);
    .data : { *(.data .data.*) } :data
    . = ALIGN(4);
    __bss_start = .;
    .bss (NOLOAD) : { *(.bss .bss.*) *(COMMON) . = ALIGN(4); } :data
    __bss_end = .;
    ASSERT(__bss_end <= 0xc000, "Standalone image exceeds low-RAM reservation/stack boundary")
    ASSERT(low_probe_image_end - low_probe_image_start <= 0x1000, "VRAM probe is too large")
    /DISCARD/ : { *(.eh_frame*) *(.comment*) *(.note*) }
}
