
#include "pci.h"

/*
 * Functions for accessing PCI base (first 256 bytes) and extended
 * (4096 bytes per PCI function) configuration space with type 1
 * accesses.
 */

#define PCI_CONF1_ADDRESS(bus, devfn, reg) \
        (0x80000000 | ((reg & 0xF00) << 16) | (bus << 16) \
        | (devfn << 8) | (reg & 0xFC))

int pci_conf1_read(unsigned int seg, unsigned int bus,
                   unsigned int devfn, int reg, int len, u32 *value)
{
        unsigned long flags;

        if (seg || (bus > 255) || (devfn > 255) || (reg > 4095)) {
                *value = -1;
                return -EINVAL;
        }

        outl(PCI_CONF1_ADDRESS(bus, devfn, reg), 0xCF8);

        switch (len) {
        case 1:
                *value = inb(0xCFC + (reg & 3));
                break;
        case 2:
                *value = inw(0xCFC + (reg & 2));
                break;
        case 3:
                *value = inw(0xCFC);
                break;
        case 4:
                *value = inl(0xCFC);
                break;
        }

        return 0;
}

int pci_conf1_write(unsigned int seg, unsigned int bus,
                    unsigned int devfn, int reg, int len, u32 value)
{       
        unsigned long flags;
        
        if (seg || (bus > 255) || (devfn > 255) || (reg > 4095))
                return -EINVAL;
        
        outl(PCI_CONF1_ADDRESS(bus, devfn, reg), 0xCF8);
        
        switch (len) {
        case 1: 
                outb((u8)value, 0xCFC + (reg & 3));
                break;
        case 2: 
                outw((u16)value, 0xCFC + (reg & 2));
                break;
        case 3: 
                outw((u16)value, 0xCFC);
                break;
        case 4: 
                outl((u32)value, 0xCFC);
                break;
        }
        
        return 0;
}


/**
 * Return an pci config space address of a device with the given
 * class/subclass id or 0 on error.
 *
 * Note: this returns the last device found!
 */
unsigned
pci_find_device_per_class(unsigned short class)
{
  unsigned i,res = 0;
  for (i=0; i<1<<13; i++)
    {
      unsigned func;
      unsigned char maxfunc = 0;
      for (func=0; func<=maxfunc; func++)
	{
	  unsigned addr = 0x80000000 | i<<11 | func<<8;
	  if (!maxfunc && pci_read_byte(addr+14) & 0x80)
	    maxfunc=7;
	  if (class == (pci_read_long(addr+0x8) >> 16))
	    res = addr;
	}
    }
  return res;
}


/**
 * Return an pci config space address of a device with the given
 * device/vendor id or 0 on error.
 */
static
unsigned
pci_find_device(unsigned id)
{
  unsigned i,res = 0;
  for (i=0; i<1<<13; i++)
    {
      unsigned func;
      unsigned char maxfunc = 0;
      for (func=0; func<=maxfunc; func++)
	{
	  unsigned addr = 0x80000000 | i<<11 | func<<8;
	  if (!maxfunc && pci_read_byte(addr+14) & 0x80)
	    maxfunc=7;
	  if (id == (pci_read_long(addr+0x8)))
	    res = addr;
	}
    }
  return res;
}


/**
 * Find a capability for a device in the capability list.
 * @param addr - address of the device in the pci config space
 * @param id   - the capability id to search.
 * @return 0 on failiure or the offset into the pci device of the capability
 */
static
unsigned char
pci_dev_find_cap(unsigned addr, unsigned char id)
{
  CHECK3(-11, !(pci_read_long(addr+PCI_CONF_HDR_CMD) & 0x100000),"no capability list support");
  unsigned char cap_offset = pci_read_byte(addr+PCI_CONF_HDR_CAP);
  while (cap_offset)
    if (id == pci_read_byte(addr+cap_offset))
      return cap_offset;
    else
      cap_offset = pci_read_byte(addr+cap_offset+PCI_CAP_OFFSET);
  return 0;
}

