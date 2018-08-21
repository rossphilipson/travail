SECTIONS {
    . = 0x00000000;
    .text : {
        . = ALIGN(4096);
        *(.sl_lz_header)
        *(.sl_first_stack)
        *(.ml_header)
        *(.text)
        *(SORT(.text*))
     }
}
