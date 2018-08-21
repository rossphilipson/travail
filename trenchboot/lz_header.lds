SECTIONS {
   . = 0x00000000;
   .sl_lz_header : { * (.sl_lz_header) }
   .lz_first_stack : { * (.lz_first_stack) }
   .rm_header : { * (.rm_header) }
   .text : { * (.text); }
}
