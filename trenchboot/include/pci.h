
#include "boot.h"


#define PCI_CONFIG_ADDR_PORT    (0x0cf8)
#define PCI_CONFIG_DATA_PORT    (0x0cfc)

#define PCI_CONF_HDR_IDX_VENDOR_ID                      0x0
#define PCI_CONF_HDR_IDX_DEVICE_ID                      0x02
#define PCI_CONF_HDR_IDX_COMMAND                        0x04
#define PCI_CONF_HDR_IDX_STATUS                         0x06
#define PCI_CONF_HDR_IDX_REVISION_ID                    0x08
#define PCI_CONF_HDR_IDX_CLASS_CODE                     0x09
#define PCI_CONF_HDR_IDX_HEADER_TYPE                    0x0E
#define PCI_CONF_HDR_IDX_CAPABILITIES_POINTER           0x34

/* PCI capability ID for SVM DEV */
#define PCI_CAPABILITIES_POINTER_ID_DEV                 0x0F

/*
 * The PCI interface treats multi-function devices as independent
 * devices.  The slot/function address of each device is encoded
 * in a single byte as follows:
 *
 *      7:3 = slot
 *      2:0 = function
 */
#define PCI_DEVFN(slot, func)   ((((slot) & 0x1f) << 3) | ((func) & 0x07))
#define PCI_SLOT(devfn)         (((devfn) >> 3) & 0x1f)
#define PCI_FUNC(devfn)         ((devfn) & 0x07)


int pci_conf1_read(unsigned int seg, unsigned int bus,
                   unsigned int devfn, int reg, int len, u32 *value);


int pci_conf1_write(unsigned int seg, unsigned int bus,
                    unsigned int devfn, int reg, int len, u32 value);
