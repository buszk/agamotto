#define pr_fmt(fmt) "periscope-i2c: " fmt

#include <linux/cdev.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/i2c.h>
#include <linux/of_irq.h>

#define BAR 0
#define CHRDEV_NAME "periscope_i2c"
#define PERISCOPE_I2C_DEVICE_ID 0x11eb
#define QEMU_VENDOR_ID 0x1234

#define DEFAULT_I2C_BOARD_ADDR 0x30 // FIXME

static struct i2c_board_info board_infos[] = {
	{ I2C_BOARD_INFO("microread", DEFAULT_I2C_BOARD_ADDR), .irq = 0 }
};

static struct i2c_adapter *adapter = NULL;

int periscope_i2c_register_adapter(struct i2c_adapter *adap)
{
	pr_info("register adapter\n");

	adapter = adap;

	return 0;
}

MODULE_LICENSE("GPL");

static struct pci_device_id id_table[] = {
	{
		PCI_DEVICE(QEMU_VENDOR_ID, PERISCOPE_I2C_DEVICE_ID),
	},
	{
		0,
	}
};

MODULE_DEVICE_TABLE(pci, id_table);

static int major;
static struct pci_dev *pdev;
static void __iomem *mmio = NULL;

static struct file_operations fops = {
	.owner = THIS_MODULE,
};

#define CMD_OFFSET 0x10
#define ARG_OFFSET 0x20
#define RET_OFFSET 0x40

static irqreturn_t irq_handler(int irq, void *dev)
{
	uint64_t r = 0;

	uint32_t arg = ioread32(mmio + ARG_OFFSET);
	uint32_t cmd = ioread32(mmio + CMD_OFFSET);

	pr_info("irq_handler irq = %d dev = %d\n", irq, *(int *)dev);
	pr_err("%s: cmd %x, arg %x\n", __FUNCTION__, cmd, arg);

	switch (cmd) {
	default:
		break;
	}

	pr_err("%s: ret %llx\n", __FUNCTION__, r);

	iowrite32(r, mmio + RET_OFFSET);

	return IRQ_HANDLED;
}

static int probe(struct pci_dev *dev, const struct pci_device_id *id)
{
	pr_info("probe\n");

	major = register_chrdev(0, CHRDEV_NAME, &fops);
	pdev = dev;

	if (pci_enable_device(dev) < 0) {
		dev_err(&(pdev->dev), "pci_enable_device\n");
		goto error;
	}

	if (pci_request_region(dev, BAR, "myregion0")) {
		dev_err(&(pdev->dev), "pci_request_region\n");
		goto error;
	}

	mmio = pci_iomap(pdev, BAR, pci_resource_len(pdev, BAR));
	if (!mmio) {
		dev_err(&(pdev->dev), "pci_iomap\n");
	}

	if (request_irq(dev->irq, irq_handler, IRQF_SHARED, "pci_irq_handler0",
			&major) < 0) {
		dev_err(&(dev->dev), "request_irq\n");
		goto error;
	}

	if (adapter != NULL) {
		struct i2c_client *client;
		client = i2c_new_device(adapter, &board_infos[0]);

		if (!client) {
			pr_err("no device\n");
			goto error;
		}

		pr_info("new device\n");
	}

	return 0;

error:
	return 1;
}

static void remove(struct pci_dev *dev)
{
	free_irq(dev->irq, &major);
	pci_release_region(dev, BAR);
	unregister_chrdev(major, CHRDEV_NAME);
}

static struct pci_driver pci_driver = {
	.name = CHRDEV_NAME,
	.id_table = id_table,
	.probe = probe,
	.remove = remove,
};

static int periscope_i2c_init(void)
{
	if (pci_register_driver(&pci_driver) < 0) {
		return 1;
	}

	return 0;
}

static void periscope_i2c_exit(void)
{
	pci_unregister_driver(&pci_driver);
}

late_initcall(periscope_i2c_init);
module_exit(periscope_i2c_exit);
