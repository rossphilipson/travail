#include <errno-base.h>
#include <pci.h>

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
