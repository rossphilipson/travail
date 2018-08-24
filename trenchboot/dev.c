

#include "types.h"
#include "pci.h"

#define DEV_PCI_BUS             0x0
#define DEV_PCI_DEVICE          0x18
#define DEV_PCI_FUNCTION        0x3


#define DEV_BASE_LO             0
#define DEV_BASE_HI             1
#define DEV_MAP                 2
#define DEV_CAP                 3
#define DEV_CR                  4
#define DEV_ERR_STATUS          5
#define DEV_ERR_ADDR_LO         6
#define DEV_ERR_ADDR_HI         7


static u32 dev_read(u32 dev, u32 function, u32 index)
{
        u32 value;

        pci_conf1_write(0, DEV_PCI_BUS, DEV_PCI_DEVICE,
			PCI_DEVFN(DEV_PCI_FUNCTION, dev + 4),
			4,
			(u32)(((function & 0xff) << 8) + (index & 0xff)) );

        pci_conf1_read(0, DEV_PCI_BUS, DEV_PCI_DEVICE,
			PCI_DEVFN(DEV_PCI_FUNCTION, dev + 8),
			4, &value);

	return value;
}

static void dev_write(u32 dev, u32 function, u32 index, u32 value)
{
        pci_conf1_write(0, DEV_PCI_BUS, DEV_PCI_DEVICE,
			PCI_DEVFN(DEV_PCI_FUNCTION, dev + 4),
			4,
			(u32)(((function & 0xff) << 8) + (index & 0xff)) );

        pci_conf1_write(0, DEV_PCI_BUS, DEV_PCI_DEVICE,
			PCI_DEVFN(DEV_PCI_FUNCTION, dev + 8),
			4, value);
}


#define DEV_MAP_V0_MASK 1<<5
#define DEV_MAP_V1_MASK 1<<11

#define DEV_CAP_REV(c)	(c & 0xFF)
#define DEV_CAP_DOMS(c)	((c & 0xFF00) >> 8)
#define DEV_CAP_MAPS(c)	((c & 0xFF0000) >> 16)

#define DEV_BASE_LO_VALID_MASK		1<<0
#define DEV_BASE_LO_PROTECTED_MASK	1<<1
#define DEV_BASE_LO_SET_SIZE(b,s)	(b & (s << 2))
#define DEV_BASE_LO_ADDR_MASK		0xFFFFF000

#define DEV_CR_ENABLE_MASK	1<<0
#define DEV_CR_MEM_CLR_MASK	1<<1
#define DEV_CR_IOSP_EN_MASK	1<<2
#define DEV_CR_MCE_EN_MASK	1<<3
#define DEV_CR_INV_CACHE_MASK	1<<4
#define DEV_CR_SL_DEV_EN_MASK	1<<5
#define DEV_CR_WALK_PROBE_MASK	1<<6

#define INVALID_CAP(c) ((c == 0) || (c == 0xF0))

u32 dev_init(u32 dev_bitmap_paddr, u32 dev_bitmap_vaddr)
{
	u8 i;
	u32 pci_cap_ptr;
	u32 next_cap_ptr;
	u32 pci_cap_id;
	u32 dev;
	/* since work with each reg one at a time, consolidate into a
	 * single reg variable to reduce stack usage
	 */
	u32 dev_map;
	u32 dev_cap;
	u32 dev_base_lo;
	u32 dev_cr;

	/* read capabilities pointer */
	pci_conf1_read(DEV_PCI_BUS, DEV_PCI_DEVICE, DEV_PCI_FUNCTION,
			PCI_CONF_HDR_IDX_CAPABILITIES_POINTER, sizeof(u32),
			&pci_cap_ptr);

	if (INVALID_CAP(pci_cap_ptr))
		return 1; /* need err num */

	next_cap_ptr = pci_cap_ptr & 0xFF;

	do {
		pci_conf1_read(DEV_PCI_BUS, DEV_PCI_DEVICE, DEV_PCI_FUNCTION,
				next_cap_ptr, sizeof(u8), &pci_cap_id);

		if (pci_cap_id == PCI_CAPABILITIES_POINTER_ID_DEV)
			break;

		pci_conf1_read(DEV_PCI_BUS, DEV_PCI_DEVICE, DEV_PCI_FUNCTION,
				next_cap_ptr, sizeof(u8), &next_cap_ptr);
	} while(next_cap_ptr != 0);


        if(INVALID_CAP(next_cap_ptr))
                return 1; /* need err num */

	dev = next_cap_ptr;

	dev_cap = dev_read(dev, DEV_CAP, 0);

	/* disable all the DEV maps. */
	dev_map &= !DEV_MAP_V0_MASK;
	dev_map &= !DEV_MAP_V1_MASK;

	for (i = 0; i < DEV_CAP_MAPS(dev_cap); i++)
		dev_write(dev, DEV_MAP, i, dev_map);


	/* set the DEV_BASE_HI and DEV_BASE_LO registers of domain 0 */
	/* DEV bitmap is within 4GB physical */
	dev_write(dev, DEV_BASE_HI, 0, 0);

	dev_base_lo = 0;
	dev_base_lo &= DEV_BASE_LO_VALID_MASK;
	dev_base_lo &= !DEV_BASE_LO_PROTECTED_MASK;

	/* since already zeroed out, no need to set to zero */
	/* dev_base_lo = DEV_BASE_LO_SET_SIZE(dev_base_lo,0) */

	dev_base_lo &= (DEV_BASE_LO_ADDR_MASK & dev_bitmap_paddr);

	dev_write(dev, DEV_BASE_LO, 0, dev_base_lo);


	/* invalidate all other domains */
	dev_base_lo &= !DEV_BASE_LO_VALID_MASK;
	dev_base_lo &= !DEV_BASE_LO_ADDR_MASK;

	for (i = 1; i < DEV_CAP_MAPS(dev_cap); i++){
		dev_write(dev, DEV_BASE_HI, i, 0);
		dev_write(dev, DEV_BASE_LO, i, dev_base_lo);
	}

	/* enable DEV protections */
	dev_cr = 0;
	dev_cr &= DEV_CR_ENABLE_MASK;
	dev_cr &= DEV_CR_IOSP_EN_MASK;
	dev_cr &= DEV_CR_SL_DEV_EN_MASK;

	dev_write(dev, DEV_CR, 0, dev_cr);

	return 0;
}

