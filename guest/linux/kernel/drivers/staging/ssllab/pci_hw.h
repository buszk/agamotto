#ifndef _SSLLAB_PCI_HW_H_
#define _SSLLAB_PCI_HW_H_

#include <asm/types.h>

struct ssllab_pci_hw;

struct ssllab_pci_hw {
	u8 __iomem *hw_addr;
	unsigned long io_base;
	void *back;
};

#endif
