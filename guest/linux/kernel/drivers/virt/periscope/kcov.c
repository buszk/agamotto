#define pr_fmt(fmt) "kcov: " fmt

#include <linux/cdev.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/kcov.h>
#include <uapi/linux/kcov_vdev.h>

#define BAR 0
#define CDEV_NAME "kcov_vdev"
#define EDU_DEVICE_ID 0x11e9
#define QEMU_VENDOR_ID 0x1234

MODULE_LICENSE("GPL");

static struct pci_device_id id_table[] = {
    { PCI_DEVICE(QEMU_VENDOR_ID, EDU_DEVICE_ID), },
    { 0, }
};
MODULE_DEVICE_TABLE(pci, id_table);
static int major;
static struct pci_dev *pdev;
static void __iomem *mmio = NULL;
static struct file_operations fops = {
    .owner   = THIS_MODULE,
};


static irqreturn_t irq_handler(int irq, void *dev)
{
    uint64_t r = 0;
    uint32_t kcov_arg = ioread32(mmio + KCOV_ARG_OFFSET);
    uint32_t kcov_cmd = ioread32(mmio + KCOV_CMD_OFFSET);
    iowrite32(0, mmio + KCOV_RESET_IRQ);
    pr_info("irq_handler irq = %d dev = %d\n", irq, *(int *)dev);
    pr_err("%s: cmd %x, arg %x\n", __FUNCTION__, kcov_cmd, kcov_arg);
    switch(kcov_cmd) {
       case KCOV_GET_AREA_OFFSET:
          r = kcov_get_area_offset_g();
          break;
       default:
          r = kcov_ioctl_locked_g(kcov_cmd, kcov_arg);
          break;
    }

    pr_err("%s: ret %llx\n", __FUNCTION__, r);
    iowrite32(r, mmio + KCOV_RET_OFFSET);
    return IRQ_HANDLED;
}

static int probe(struct pci_dev *dev, const struct pci_device_id *id)
{
    major = register_chrdev(0, CDEV_NAME, &fops);
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
    if (request_irq(dev->irq, irq_handler, IRQF_SHARED, "pci_irq_handler0", &major) < 0) {
        dev_err(&(dev->dev), "request_irq\n");
        goto error;
    }
    return 0;
error:
    return 1;
}

static void remove(struct pci_dev *dev)
{
    free_irq(dev->irq, &major);
    pci_release_region(dev, BAR);
    unregister_chrdev(major, CDEV_NAME);
}

static void cov_enable(void *area, unsigned int size, bool remote) // TODO: size
{
    if (mmio) {
        if (remote)
            iowrite32(virt_to_phys(area), mmio + KCOV_COV_REMOTE_ENABLE);
        else
            iowrite32(virt_to_phys(area), mmio + KCOV_COV_ENABLE);
    }
}

static void cov_disable(void *area, bool remote) // TODO: size
{
    if (mmio) {
        if (remote)
            iowrite32(virt_to_phys(area), mmio + KCOV_COV_REMOTE_DISABLE);
        else
            iowrite32(virt_to_phys(area), mmio + KCOV_COV_DISABLE);
    }
}

static void cov_flush(void *area, unsigned int size)
{
    if (mmio) {
        iowrite32(virt_to_phys(area), mmio + KCOV_COV_FULL);
    }
}

static struct pci_driver pci_driver = {
    .name     = CDEV_NAME,
    .id_table = id_table,
    .probe    = probe,
    .remove   = remove,
};

#define MAP_SIZE_POW2 16
#define MAP_SIZE (1 << MAP_SIZE_POW2)

static int myinit(void)
{
    uint32_t ccmode;
    uint64_t area_offset;

    if (pci_register_driver(&pci_driver) < 0) {
        return 1;
    }

    if (!mmio) {
        return 1;
    }

    ccmode = ioread32(mmio + KCOV_CCMODE_OFFSET);
    pr_info("tracing mode: 0x%x\n", ccmode);

#define TRACE_PC_FLUSH_FLAG (unsigned int)0x10000

    if ((ccmode & ~TRACE_PC_FLUSH_FLAG) == KCOV_TRACE_PC) {
        kcov_register_enable_callback(cov_enable);
        kcov_register_disable_callback(cov_disable);
        if (ccmode & TRACE_PC_FLUSH_FLAG) {
            kcov_register_flush_callback(cov_flush);
        }
    }

    if (ioread32(mmio + KCOV_GMODE_OFFSET)) {
        pr_info("tracing mode: global\n");

        if (kcov_open_g() != 0) {
            pr_err("failed to open kcov\n");
        }
        if (kcov_ioctl_locked_g(KCOV_INIT_TRACE, MAP_SIZE/sizeof(unsigned long)) != 0) {
            pr_err("failed to init kcov\n");
        }
        if (kcov_mmap_g() != 0) {
            pr_err("failed to mmap kcov\n");
        }
        if (kcov_ioctl_locked_g(KCOV_ENABLE, ccmode) != 0) {
            pr_err("failed to enable kcov\n");
        }

        area_offset = kcov_get_area_offset_g();
        iowrite32(area_offset, mmio + KCOV_RET_OFFSET);
    }

    return 0;
}

static void myexit(void)
{
    kcov_close_g();
    pci_unregister_driver(&pci_driver);
}

rootfs_initcall(myinit);
module_exit(myexit);
