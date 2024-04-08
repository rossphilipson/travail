#ifndef _DEBUG_MEMMAP_H_
#define _DEBUG_MEMMAP_H_

#define E820_TYPE_RAM_DBG		1
#define E820_TYPE_RESERVED_DBG		2
#define E820_TYPE_ACPI_DBG		3
#define E820_TYPE_NVS_DBG		4
#define E820_TYPE_UNUSABLE_DBG		5
#define E820_TYPE_PMEM_DBG		7
#define E820_TYPE_PRAM_DBG		12
#define E820_TYPE_SOFT_RESERVED_DBG 	0xefffffff
#define E820_TYPE_RESERVED_KERN_DBG	128

struct e820_entry_dbg {
	unsigned long addr;
	unsigned long size;
	unsigned int type;
} __attribute__((packed));

static void e820_print_type_dbg(unsigned int type)
{
	switch (type) {
	case E820_TYPE_RAM_DBG:			/* Fall through */
	case E820_TYPE_RESERVED_KERN_DBG:	printd_str("usable\n");			break;
	case E820_TYPE_RESERVED_DBG:		printd_str("reserved\n");		break;
	case E820_TYPE_SOFT_RESERVED_DBG:	printd_str("soft reserved\n");		break;
	case E820_TYPE_ACPI_DBG:		printd_str("ACPI data\n");		break;
	case E820_TYPE_NVS_DBG:			printd_str("ACPI NVS\n");		break;
	case E820_TYPE_UNUSABLE_DBG:		printd_str("unusable\n");		break;
	case E820_TYPE_PMEM_DBG:		/* Fall through_DBG: */
	case E820_TYPE_PRAM_DBG:		printd_str("persistent mem\n");		break;
	default:				printd_str("persistent RAM \n");	break;
	}
}

static void e820_print_dbg(struct e820_entry_dbg *entries, int nr_entries)
{
	int i;

	for (i = 0; i < nr_entries; i++) {
		printd_str("e820-Entry-DBG: [mem ");
		printd_hex(entries[i].addr);
		printd_str("-");
		printd_hex(entries[i].addr + entries[i].size - 1);
		e820_print_type_dbg(entries[i].type);
		printd_str("\n");
	}
}

#endif
