// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2019 Oracle and/or its affiliates. All rights reserved.
 * Copyright (c) 2019 Apertus Solutions, LLC
 *
 * Author(s):
 *     Daniel P. Smith <dpsmith@apertussolutions.com>
 *
 */

#include <linux/fs.h>
#include <linux/init.h>
#include <linux/linkage.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/security.h>
#include <linux/memblock.h>
#include <asm/segment.h>
#include <asm/sections.h>
#include <asm/boot.h>
#include <asm/msr.h>
#include <asm/processor-flags.h>
#include <asm/asm-offsets.h>
#include <asm/e820/api.h>
#include <asm/bootparam.h>
#include <asm/setup.h>
#include <asm/slaunch.h>

#define PREFIX	"SLAUNCH: "

static u32 sl_flags;

/* This should be plenty of room */
static u8 txt_dmar[PAGE_SIZE] __aligned(16);

u32 slaunch_get_flags(void)
{
	return sl_flags;
}

struct acpi_table_header *slaunch_get_dmar_table(struct acpi_table_header *dmar)
{
	/* The DMAR is only stashed and provided via TXT on Intel systems */
	if (memcmp(txt_dmar, "DMAR", 4))
		return dmar;

	return (struct acpi_table_header *)(&txt_dmar[0]);
}

static void slaunch_txt_reset(void __iomem *txt, const char *msg, u64 error)
{
	u64 one = 1, val;

	printk(KERN_ERR PREFIX "%s", msg);

	memcpy_toio(txt + TXTCR_ERRORCODE, &error, sizeof(u64));
	memcpy_fromio(&val, txt + TXTCR_E2STS, sizeof(u64));
	memcpy_toio(txt + TXTCR_CMD_UNLOCK_MEM_CONFIG, &one, sizeof(u64));
	memcpy_fromio(&val, txt + TXTCR_E2STS, sizeof(u64));
	memcpy_toio(txt + TXTCR_CMD_RESET, &one, sizeof(u64));

	for ( ; ; )
		__asm__ __volatile__ ("pause");
}

/*
 * The TXT heap is too big to map all at once with early_ioremap
 * so it is done a table at a time.
 */
void __iomem *txt_early_get_heap_table(u32 type, u32 bytes)
{
	void __iomem *txt;
	void __iomem *heap;
	u64 base, size, offset = 0;
	int i;

	if (type > TXT_SINIT_MLE_DATA_TABLE)
		panic("Error invalid type for early heap walk\n");

	txt = early_ioremap(TXT_PRIV_CONFIG_REGS_BASE,
			    TXT_NR_CONFIG_PAGES * PAGE_SIZE);
	if (!txt) {
		/* This should not occur, no recovery possible */
		panic("Error early_ioremap of TXT registers for heap walk\n");
	}

	memcpy_fromio(&base, txt + TXTCR_HEAP_BASE, sizeof(u64));
	memcpy_fromio(&size, txt + TXTCR_HEAP_SIZE, sizeof(u64));

	/* Iterate over heap tables looking for table of "type" */
	for (i = 0; i < type; i++) {
		base += offset;
		heap = early_ioremap(base, sizeof(u64));
		if (!heap)
			slaunch_txt_reset(txt,
				"Error early_ioremap of heap for heap walk\n",
				TXT_SLERROR_HEAP_WALK);

		memcpy_fromio(&offset, heap, sizeof(u64));

		/*
		 * After the first iteration, any offset of zero is invalid and
		 * implies the TXT heap is corrupted.
		 */
		if (!offset)
			slaunch_txt_reset(txt,
				"Error invalid 0 offset in heap walk\n",
				TXT_SLERROR_HEAP_ZERO_OFFSET);

		early_iounmap(heap, sizeof(u64));
	}

	/* Skip the size field at the head of each table */
	base += sizeof(u64);
	heap = early_ioremap(base, bytes);
	if (!heap)
		slaunch_txt_reset(txt,
				  "Error early_ioremap of heap section\n",
				  TXT_SLERROR_HEAP_MAP);

	early_iounmap(txt, TXT_NR_CONFIG_PAGES * PAGE_SIZE);

	return heap;
}

#define PMR_LO_BASE	0
#define PMR_LO_SIZE	1
#define PMR_HI_BASE	2
#define PMR_HI_SIZE	3

/*
 * TXT uses a special set of VTd registers to protect all of memory from DMA
 * until the IOMMU can be programmed to protect memory. There is the low
 * memory PMR that can protect all memory up to 4G. The high memory PRM can
 * be setup to protect all memory beyond 4Gb. Validate that these values cover
 * what is expected.
 */
static void slaunch_verify_pmrs(void __iomem *txt)
{
	void __iomem *heap;
	u64 pmrvals[4];
	unsigned long last_pfn;

	heap = txt_early_get_heap_table(TXT_OS_SINIT_DATA_TABLE,
					TXT_OS_SINIT_LO_PMR_BASE +
					sizeof(pmrvals));
	if (!heap)
		slaunch_txt_reset(txt,
				  "Error early_ioremap of PMR values\n",
				  TXT_SLERROR_PMR_VALS);
	memcpy_fromio(&pmrvals[0], heap + TXT_OS_SINIT_LO_PMR_BASE,
		      sizeof(pmrvals));

	early_iounmap(heap, TXT_OS_SINIT_LO_PMR_BASE + sizeof(pmrvals));

	last_pfn = e820__end_of_ram_pfn();

	/*
	 * First make sure the hi PMR covers all memory above 4G. In the
	 * unlikely case where there is < 4G on the system, the hi PMR will
	 * not be set.
	 */
	if (pmrvals[PMR_HI_BASE] != 0x0ULL) {
		if (pmrvals[PMR_HI_BASE] != 0x100000000ULL)
			slaunch_txt_reset(txt,
					  "Error hi PMR base\n",
					  TXT_SLERROR_HI_PMR_BASE);

		if (last_pfn << PAGE_SHIFT >
		    pmrvals[PMR_HI_BASE] + pmrvals[PMR_HI_SIZE])
			slaunch_txt_reset(txt,
					  "Error hi PMR size\n",
					  TXT_SLERROR_HI_PMR_SIZE);
	}

	/* Lo PMR base should always be 0 */
	if (pmrvals[PMR_LO_BASE] != 0x0ULL)
		slaunch_txt_reset(txt,
				  "Error lo PMR base\n",
				  TXT_SLERROR_LO_PMR_BASE);

	/*
	 * Check that if the kernel was loaded below 4G, that it is protected
	 * by the lo PMR.
	 */
	if (__pa_symbol(_end) > pmrvals[PMR_LO_SIZE])
		slaunch_txt_reset(txt,
				  "Error lo PMR does not cover MLE kernel\n",
				  TXT_SLERROR_LO_PMR_MLE);
}

static int slaunch_txt_reserve_range(u64 base, u64 size)
{
	int type;

	type = e820__get_entry_type(base, base + size - 1);
	if (type == E820_TYPE_RAM) {
		e820__range_update(base, size, E820_TYPE_RAM,
				   E820_TYPE_RESERVED);
		return 1;
	}

	return 0;
}

#define MDRS_NUM	0
#define MDRS_OFFSET	1

/*
 * For Intel, certain reqions of memory must be marked as reserved in the e820
 * memory map if they are not already. This includes the TXT HEAP, the ACM area,
 * the TXT private register bank. Normally these are properly reserved by
 * firmware but if it was not done, do it now.
 *
 * Also the Memory Descriptor Ranges that are passed to the MLE (see TXT
 * specification) may need to be reserved depeding on their type.
 */
static void slaunch_txt_reserve(void __iomem *txt)
{
	struct txt_memory_descriptor_record *mdr;
	void __iomem *heap;
	u64 base, size;
	u32 mdrvals[2];
	u32 mdrslen;
	u32 i;
	int updated = 0;

	base = TXT_PRIV_CONFIG_REGS_BASE;
	size = TXT_PUB_CONFIG_REGS_BASE - TXT_PRIV_CONFIG_REGS_BASE;
	updated += slaunch_txt_reserve_range(base, size);

	memcpy_fromio(&base, txt + TXTCR_HEAP_BASE, sizeof(u64));
	memcpy_fromio(&size, txt + TXTCR_HEAP_SIZE, sizeof(u64));
	updated += slaunch_txt_reserve_range(base, size);

	memcpy_fromio(&base, txt + TXTCR_SINIT_BASE, sizeof(u64));
	memcpy_fromio(&size, txt + TXTCR_SINIT_SIZE, sizeof(u64));
	updated += slaunch_txt_reserve_range(base, size);

	heap = txt_early_get_heap_table(TXT_SINIT_MLE_DATA_TABLE,
					TXT_SINIT_MLE_NUMBER_MDRS +
					sizeof(mdrvals));
	if (!heap)
		slaunch_txt_reset(txt,
				  "Error early_ioremap of MDR values\n",
				  TXT_SLERROR_HEAP_MDR_VALS);
	memcpy_fromio(&mdrvals[MDRS_NUM], heap + TXT_SINIT_MLE_NUMBER_MDRS,
		      sizeof(mdrvals));

	early_iounmap(heap, TXT_SINIT_MLE_NUMBER_MDRS + sizeof(mdrvals));

	if (!mdrvals[MDRS_NUM])
		goto out;

	mdrslen = (mdrvals[MDRS_NUM]*
		   sizeof(struct txt_memory_descriptor_record));

	heap = txt_early_get_heap_table(TXT_SINIT_MLE_DATA_TABLE,
					mdrvals[MDRS_OFFSET] + mdrslen);
	if (!heap)
		slaunch_txt_reset(txt,
				  "Error early_ioremap of MDRs\n",
				  TXT_SLERROR_HEAP_MDRS_MAP);

	mdr = (struct txt_memory_descriptor_record *)
			(heap + mdrvals[MDRS_OFFSET] - 8);

	for (i = 0; i < mdrvals[MDRS_NUM]; i++, mdr++) {
		/* Spec says some entries can have length 0, ignore them */
		if (mdr->type > 0 && mdr->length > 0)
			updated += slaunch_txt_reserve_range(mdr->address,
							     mdr->length);
	}

	early_iounmap(heap, mdrvals[MDRS_OFFSET] + mdrslen);

out:
	if (updated) {
		e820__update_table(e820_table);
		pr_info("TXT altered physical RAM map:\n");
		e820__print_table("TXT-reserve");
	}
}

#define DMAR_SIZE	0
#define DMAR_OFFSET	1

/*
 * TXT stashes a safe copy of the DMAR ACPI table to prevent tampering.
 * It is stored in the TXT heap. Fetch it from there and make it available
 * to the IOMMU driver.
 */
static void slaunch_copy_dmar_table(void __iomem *txt)
{
	void __iomem *heap;
	u32 dmarvals[2];

	memset(&txt_dmar, 0, PAGE_SIZE);

	heap = txt_early_get_heap_table(TXT_SINIT_MLE_DATA_TABLE,
					TXT_SINIT_MLE_DMAR_TABLE_SIZE +
					sizeof(dmarvals));
	if (!heap)
		slaunch_txt_reset(txt,
				  "Error early_ioremap of DMAR values\n",
				  TXT_SLERROR_HEAP_DMAR_VALS);
	memcpy_fromio(&dmarvals[0], heap + TXT_SINIT_MLE_DMAR_TABLE_SIZE,
		      sizeof(dmarvals));

	early_iounmap(heap, TXT_SINIT_MLE_DMAR_TABLE_SIZE + sizeof(dmarvals));

	if (!dmarvals[DMAR_SIZE] || !dmarvals[DMAR_OFFSET])
		slaunch_txt_reset(txt,
				  "Error invalid DMAR table values\n",
				  TXT_SLERROR_HEAP_INVALID_DMAR);

	if (unlikely(dmarvals[DMAR_SIZE] > PAGE_SIZE))
		slaunch_txt_reset(txt,
				  "Error DMAR too big to store\n",
				  TXT_SLERROR_HEAP_DMAR_SIZE);


	heap = txt_early_get_heap_table(TXT_SINIT_MLE_DATA_TABLE,
					dmarvals[DMAR_SIZE]);
	if (!heap)
		slaunch_txt_reset(txt,
				  "Error early_ioremap of DMAR\n",
				  TXT_SLERROR_HEAP_DMAR_MAP);

	memcpy_fromio(&txt_dmar[DMAR_SIZE],
		      (void *)(heap + dmarvals[DMAR_OFFSET] - 8),
		      dmarvals[DMAR_SIZE]);

	early_iounmap(heap, dmarvals[DMAR_SIZE]);
}

/*
 * Intel specific late stub setup and validation.
 */
static void slaunch_setup_intel(void)
{
	void __iomem *txt;
	u64 val = 0x1ULL;

	txt = early_ioremap(TXT_PRIV_CONFIG_REGS_BASE,
			    TXT_NR_CONFIG_PAGES * PAGE_SIZE);
	if (!txt) {
		/* This is really bad, no where to go from here */
		panic("Error early_ioremap of TXT registers\n");
	}

	/*
	 * Try to read the Intel VID from the TXT private registers to see if
	 * TXT is active and the measured launch happened.
	 */
	memcpy_fromio(&val, txt + TXTCR_DIDVID, sizeof(u64));
	if ((u16)(val & 0xffff) != 0x8086) {
		/*
		 * Can't do a proper TXT reset since it appears we are not in
		 * SMX mode.
		 */
		panic("Invalid TXT vendor ID, not in SMX mode\n");
	}

	/*
	 * Reading the proper DIDVID from the private register space means we
	 * are in SMX mode and private registers are open for read/write.
	 */

	/* On Intel, have to handle TPM localities via TXT */
	val = 0x1ULL;
	memcpy_toio(txt + TXTCR_CMD_SECRETS, &val, sizeof(u64));
	memcpy_fromio(&val, txt + TXTCR_E2STS, sizeof(u64));
	val = 0x1ULL;
	memcpy_toio(txt + TXTCR_CMD_OPEN_LOCALITY1, &val, sizeof(u64));
	memcpy_fromio(&val, txt + TXTCR_E2STS, sizeof(u64));

	slaunch_verify_pmrs(txt);

	slaunch_txt_reserve(txt);

	slaunch_copy_dmar_table(txt);

	early_iounmap(txt, TXT_NR_CONFIG_PAGES * PAGE_SIZE);

	pr_info("Intel TXT setup complete\n");
}

void slaunch_setup(void)
{
	u32 vendor[4];

	/*
	 * First assume Secure Launch is enabled and this is a
	 * supported platform.
	 */
	sl_flags = SL_FLAG_ACTIVE;

	/* Get manufacturer string with CPUID 0 */
	cpuid(0, &vendor[0], &vendor[1], &vendor[2], &vendor[3]);

	if (vendor[1] == INTEL_CPUID_MFGID_EBX &&
	    vendor[2] == INTEL_CPUID_MFGID_ECX &&
	    vendor[3] == INTEL_CPUID_MFGID_EDX) {
		sl_flags |= SL_FLAG_ARCH_TXT;
		slaunch_setup_intel();
	} else if (vendor[1] == AMD_CPUID_MFGID_EBX &&
		   vendor[2] == AMD_CPUID_MFGID_ECX &&
		   vendor[3] == AMD_CPUID_MFGID_EDX) {
		panic("Invalid platform: AMD not supported\n");
	} else {
		panic("Invalid platform: not Intel or AMD\n");
	}
}

/*
 * Securityfs exposure
 */
struct memfile {
	char *name;
	void __iomem *addr;
	size_t size;
};

static struct memfile sl_evtlog = {"eventlog", 0, 0};
static void __iomem *txt_heap;

static ssize_t sl_evtlog_read(struct file *file, char __user *buf,
			      size_t count, loff_t *pos)
{
	return simple_read_from_buffer(buf, count, pos,
		sl_evtlog.addr, sl_evtlog.size);
}

static const struct file_operations sl_evtlog_ops = {
	.read = sl_evtlog_read,
	.llseek	= default_llseek,
};

#define SL_DIR_ENTRY	1 /* directoy node must be last */
#define SL_FS_ENTRIES	2

static struct dentry *fs_entries[SL_FS_ENTRIES];

static long slaunch_expose_securityfs(void)
{
	long ret = 0;
	int entry = SL_DIR_ENTRY;

	fs_entries[entry] = securityfs_create_dir("slaunch", NULL);
	if (IS_ERR(fs_entries[entry])) {
		pr_err("Error creating securityfs sl_evt_log directory\n");
		ret = PTR_ERR(fs_entries[entry]);
		goto err;
	}

	if (sl_evtlog.addr > 0) {
		entry--;
		fs_entries[entry] = securityfs_create_file(sl_evtlog.name,
					   S_IRUSR | S_IRGRP,
					   fs_entries[SL_DIR_ENTRY], NULL,
					   &sl_evtlog_ops);
		if (IS_ERR(fs_entries[entry])) {
			pr_err("Error creating securityfs %s file\n",
				sl_evtlog.name);
			ret = PTR_ERR(fs_entries[entry]);
			goto err_dir;
		}
	}

	return 0;

err_dir:
	securityfs_remove(fs_entries[SL_DIR_ENTRY]);
err:
	return ret;
}

static void slaunch_teardown_securityfs(void)
{
	int i;

	for (i = 0; i < SL_FS_ENTRIES; i++)
		securityfs_remove(fs_entries[i]);

	if (sl_flags & SL_FLAG_ARCH_TXT) {
		if (txt_heap) {
			iounmap(txt_heap);
			txt_heap = NULL;
		}
	}

	sl_evtlog.addr = 0;
	sl_evtlog.size = 0;
}

static void slaunch_intel_evtlog(void)
{
	void __iomem *config;
	struct txt_os_mle_data *params;

	config = ioremap(TXT_PUB_CONFIG_REGS_BASE, TXT_NR_CONFIG_PAGES *
			 PAGE_SIZE);
	if (!config) {
		pr_err("Error failed to ioremap TXT reqs\n");
		return;
	}

	/* now map TXT heap */
	txt_heap = ioremap(*(u64 *)(config + TXTCR_HEAP_BASE),
		    *(u64 *)(config + TXTCR_HEAP_SIZE));
	iounmap(config);
	if (!txt_heap) {
		pr_err("Error failed to ioremap TXT heap\n");
		return;
	}

	params = (struct txt_os_mle_data *)txt_os_mle_data_start(txt_heap);

	sl_evtlog.size = TXT_MAX_EVENT_LOG_SIZE;
	sl_evtlog.addr = (void __iomem *)&params->event_log_buffer[0];
}

static int __init slaunch_late_init(void)
{
	if (sl_flags & SL_FLAG_ARCH_TXT)
		slaunch_intel_evtlog();
	else
		BUG();

	return slaunch_expose_securityfs();
}

static void __exit slaunch_exit(void)
{
	slaunch_teardown_securityfs();
}

late_initcall(slaunch_late_init);

__exitcall(slaunch_exit);
