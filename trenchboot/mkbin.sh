#! /bin/sh

cp -f lz_header lz_header.elf

objcopy --remove-section .interp lz_header
objcopy --remove-section .dynsym lz_header
objcopy --remove-section .dynstr lz_header
objcopy --remove-section .hash lz_header
objcopy --remove-section .eh_frame lz_header
objcopy --remove-section .dynamic lz_header
objcopy --remove-section .got.plt lz_header
objcopy --remove-section .signature lz_header
objcopy --remove-section .comment lz_header
objcopy --remove-section .symtab lz_header
objcopy --remove-section .strtab lz_header
objcopy --remove-section .shstrtab lz_header

# Make flat binary image
objcopy -O binary lz_header lz_header.bin

# Plus some debug files
objdump -d lz_header.elf > lz_header.dsm
hexdump -C lz_header.bin > lz_header.hex
