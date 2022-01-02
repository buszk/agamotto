#include "pci_hw.h"
#include <linux/pci.h>
#include <linux/io.h>
#include <linux/module.h>

#define BAR_0 0
#define BAR_1 1
#define BAR_5 5

#define ssllab_info(format, arg...) pr_info("SSLLAB-PCI: " format, ##arg)
#define ssllab_err(format, arg...) pr_err("SSLLAB-PCI: " format, ##arg)

struct pciex_adapter {
	struct ssllab_pci_hw hw;

	/* OS defined structs */
	struct pci_dev *pdev;

	/* Interrupt Throttle Rate */
	u32 itr;
	int bars;
	int need_ioport;
};

char pciex_driver_name[] = "ssllab-pci";
static char pciex_driver_string[] = "SSLLAB PCI Example Driver";
#define DRV_VERSION "v0.1"
const char pciex_driver_version[] = DRV_VERSION;

#define PCI_VENDOR_ID_SSLLAB 0xDEAD
#define SSLLAB_PCIEX_DEVICE(device_id)                                         \
	{                                                                      \
		PCI_DEVICE(PCI_VENDOR_ID_SSLLAB, device_id)                    \
	}

/* pciex_pci_tbl - PCI Device ID Table
 *
 * Last entry must be all 0s
 */
static const struct pci_device_id pciex_pci_tbl[] = {
    SSLLAB_PCIEX_DEVICE(0x1000),
    SSLLAB_PCIEX_DEVICE(0x1001),
    /* SSLLAB_PCIEX_DEVICE(0x1004), */
    /* SSLLAB_PCIEX_DEVICE(0x1008), */
    /* SSLLAB_PCIEX_DEVICE(0x1009), */
    /* SSLLAB_PCIEX_DEVICE(0x100C), */
    /* SSLLAB_PCIEX_DEVICE(0x100D), */
    /* SSLLAB_PCIEX_DEVICE(0x100E), */
    /* SSLLAB_PCIEX_DEVICE(0x100F), */
    /* SSLLAB_PCIEX_DEVICE(0x1010), */
    /* SSLLAB_PCIEX_DEVICE(0x1011), */
    /* SSLLAB_PCIEX_DEVICE(0x1012), */
    /* SSLLAB_PCIEX_DEVICE(0x1013), */
    /* SSLLAB_PCIEX_DEVICE(0x1014), */
    /* SSLLAB_PCIEX_DEVICE(0x1015), */
    /* SSLLAB_PCIEX_DEVICE(0x1016), */
    /* SSLLAB_PCIEX_DEVICE(0x1017), */
    /* SSLLAB_PCIEX_DEVICE(0x1018), */
    /* SSLLAB_PCIEX_DEVICE(0x1019), */
    /* SSLLAB_PCIEX_DEVICE(0x101A), */
    /* SSLLAB_PCIEX_DEVICE(0x101D), */
    /* SSLLAB_PCIEX_DEVICE(0x101E), */
    /* SSLLAB_PCIEX_DEVICE(0x1026), */
    /* SSLLAB_PCIEX_DEVICE(0x1027), */
    /* SSLLAB_PCIEX_DEVICE(0x1028), */
    /* SSLLAB_PCIEX_DEVICE(0x1075), */
    /* SSLLAB_PCIEX_DEVICE(0x1076), */
    /* SSLLAB_PCIEX_DEVICE(0x1077), */
    /* SSLLAB_PCIEX_DEVICE(0x1078), */
    /* SSLLAB_PCIEX_DEVICE(0x1079), */
    /* SSLLAB_PCIEX_DEVICE(0x107A), */
    /* SSLLAB_PCIEX_DEVICE(0x107B), */
    /* SSLLAB_PCIEX_DEVICE(0x107C), */
    /* SSLLAB_PCIEX_DEVICE(0x108A), */
    /* SSLLAB_PCIEX_DEVICE(0x1099), */
    /* SSLLAB_PCIEX_DEVICE(0x10B5), */
    /* SSLLAB_PCIEX_DEVICE(0x2E6E), */
    /* required last entry */
    {
	0,
    }};

MODULE_DEVICE_TABLE(pci, pciex_pci_tbl);

static irqreturn_t pciex_intr(int irq, void *data);
static int pciex_probe(struct pci_dev *pdev, const struct pci_device_id *ent);
static void pciex_remove(struct pci_dev *pdev);
static void pciex_shutdown(struct pci_dev *pdev);

static pci_ers_result_t pciex_io_error_detected(struct pci_dev *pdev,
						pci_channel_state_t state);
static pci_ers_result_t pciex_io_slot_reset(struct pci_dev *pdev);
static void pciex_io_resume(struct pci_dev *pdev);

static const struct pci_error_handlers pciex_err_handler = {
    .error_detected = pciex_io_error_detected,
    .slot_reset = pciex_io_slot_reset,
    .resume = pciex_io_resume,
};

static struct pci_driver pciex_driver = {.name = pciex_driver_name,
					 .id_table = pciex_pci_tbl,
					 .probe = pciex_probe,
					 .remove = pciex_remove,
#ifdef CONFIG_PM
/* Power Managerment Hooks */
#endif
					 .shutdown = pciex_shutdown,
					 .err_handler = &pciex_err_handler};

MODULE_AUTHOR("Dokyung Song, <dokyung.song@gmail.com>");
MODULE_DESCRIPTION("SSLLAB PCI Example Driver");
MODULE_LICENSE("GPL");
MODULE_VERSION(DRV_VERSION);

static int __init pciex_init_module(void)
{
	int ret;
	pr_info("%s - version %s\n", pciex_driver_string, pciex_driver_version);

	ret = pci_register_driver(&pciex_driver);

	return ret;
}

module_init(pciex_init_module);

static void __exit pciex_exit_module(void)
{
	pci_unregister_driver(&pciex_driver);
}

module_exit(pciex_exit_module);

static int pciex_request_irq(struct pci_dev *pdev)
{
	struct pciex_adapter *adapter = pci_get_drvdata(pdev);
	irq_handler_t handler = pciex_intr;
	int irq_flags = IRQF_SHARED;
	int err;
	u8 irq;

	err = pci_read_config_byte(pdev, PCI_INTERRUPT_LINE, &irq);

	if (err) {
		ssllab_err("Unable to read PCI interrupt line Error: %d\n",
			   err);
		goto request_irq_err;
	}

	ssllab_info("PCI interrupt line: %d\n", irq);

	err = devm_request_irq(&pdev->dev, irq, handler, irq_flags,
			       pciex_driver.name, adapter);

	if (err) {
		ssllab_err("Unable to allocate interrupt Error: %d\n", err);
	}

request_irq_err:
	return err;
}

static void pciex_irq_disable(struct pciex_adapter *adapter) {}

static int pciex_probe(struct pci_dev *pdev, const struct pci_device_id *ent)
{
	struct pciex_adapter *adapter = NULL;
	struct ssllab_pci_hw *hw;
	int i, err;
	int bars;
	bool disable_dev = false;

	bars = pci_select_bars(pdev, IORESOURCE_MEM | IORESOURCE_IO);
	err = pci_enable_device(pdev);

	if (err)
		return err;

	err = pci_request_selected_regions(pdev, bars, pciex_driver_name);
	if (err)
		goto err_pci_reg;

	pci_set_master(pdev);
	err = pci_save_state(pdev);
	if (err)
		goto err_alloc_etherdev;

	adapter = kzalloc(sizeof(struct pciex_adapter), GFP_KERNEL);
	if (adapter == NULL)
		return -ENOMEM;
	adapter->pdev = pdev;
	adapter->bars = bars;
	adapter->need_ioport = true;

	pci_set_drvdata(pdev, adapter);

	hw = &adapter->hw;
	hw->back = adapter;

	hw->hw_addr = pci_ioremap_bar(pdev, BAR_0);
	if (!hw->hw_addr)
		goto err_ioremap;

	if (adapter->need_ioport) {
		for (i = BAR_1; i <= BAR_5; i++) {
			if (pci_resource_len(pdev, i) == 0)
				continue;
			if (pci_resource_flags(pdev, i) & IORESOURCE_IO) {
				hw->io_base = pci_resource_start(pdev, i);
			}
		}
	}

	if (pciex_request_irq(pdev)) {
		goto err_pci_irq;
	}

	ssllab_info("request irq successful\n");

	/* ping the hardware */
	iowrite32(0xBEEF, hw->hw_addr);

	/* reset the hardware */
	// TODO

	ssllab_info("probe successful\n");

	return 0;

err_ioremap:
err_pci_irq:
	kfree(adapter);
	adapter = NULL;

err_alloc_etherdev:
	pci_release_selected_regions(pdev, bars);

err_pci_reg:
	if (!adapter || disable_dev)
		pci_disable_device(pdev);
	return err;
}

static void pciex_remove(struct pci_dev *pdev) {}

static irqreturn_t pciex_intr(int irq, void *data)
{
	struct pciex_adapter *adapter = data;
	struct ssllab_pci_hw *hw = &adapter->hw;
	u32 icr = readl(hw->hw_addr);

	if (unlikely((!icr)))
		return IRQ_NONE;  /* Not our interrupt */

	iowrite32(0x0, hw->hw_addr);
	ssllab_info("irq handled\n");
	return IRQ_HANDLED;
}

static pci_ers_result_t pciex_io_error_detected(struct pci_dev *pdev,
						pci_channel_state_t state)
{
	/* Request a slot slot reset. */
	return PCI_ERS_RESULT_NEED_RESET;
}

static pci_ers_result_t pciex_io_slot_reset(struct pci_dev *pdev)
{

	return PCI_ERS_RESULT_RECOVERED;
}

static void pciex_io_resume(struct pci_dev *pdev) {}

static void pciex_shutdown(struct pci_dev *pdev) {}
