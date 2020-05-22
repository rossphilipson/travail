#ifndef __DEFS_H__
#define __DEFS_H__

#if __WORDSIZE == 64
#define INT_FMT "%ld"
#define UINT_FMT "%lx"
#else
#define INT_FMT "%d"
#define UINT_FMT "%x"
#endif

#define ACPI_SIG_DMAR	"DMAR"	/* DMA Remapping table */

uint8_t *helper_mmap(size_t phys_addr, size_t length);
void helper_unmmap(uint8_t *addr, size_t length);
int helper_efi_locate(const char *efi_entry, uint32_t length, size_t *location);

int acpi_get_dmar(uint8_t *dmar_buf, uint32_t length);

#endif /* __DEFS_H__ */
