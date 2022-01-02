#include <linux/cdev.h>
#include <linux/dma-mapping.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>
#include "kvm_api_test.h"

#define BAR 0
#define CDEV_NAME "kvm_api_test"
#define EDU_DEVICE_ID 0x11ea
#define QEMU_VENDOR_ID 0x1234

MODULE_LICENSE("GPL");
#define N_STEPS 4

static struct pci_device_id id_table[] = {
    { PCI_DEVICE(QEMU_VENDOR_ID, EDU_DEVICE_ID), },
    { 0, }
};
MODULE_DEVICE_TABLE(pci, id_table);
static int major;
static struct pci_dev *pdev;
static void __iomem *mmio;
static struct file_operations fops = {
    .owner   = THIS_MODULE,
};

static int coherent_test1(struct device *d, unsigned int size)
{
   int i;
   dma_addr_t dma_addr;
	char* mem = dma_alloc_coherent(d, size, &dma_addr, GFP_KERNEL);
   if(!mem) {
      pr_err("%s: Error dma_alloc_coherent\n", __FUNCTION__);
      return -1;
   }
   iowrite32(dma_addr, mmio + KVM_TEST_DMA_MAP_ADDR);
   iowrite32(size, mmio + KVM_TEST_DMA_MAP_SIZE);
   iowrite32(0, mmio + KVM_TEST_DMA_MAP);
   pr_err("%s: coherent map @ p %llx, v %lx, s %d\n", __FUNCTION__, dma_addr, (unsigned long)mem, size);
   for(i=0; i<size; i+=(size/N_STEPS)) {
      mem[i] = mem[i] + 1;
   }
   dma_free_coherent(d, size, mem, dma_addr);
   iowrite32(dma_addr, mmio + KVM_TEST_DMA_UNMAP_ADDR);
   iowrite32(0, mmio + KVM_TEST_DMA_UNMAP);
   return 0;
}

static int coherent_testn(struct device *d, unsigned int size, unsigned int n)
{
   int ni, i;
   int r = 0;

   dma_addr_t *dma_addrs = (dma_addr_t*)kmalloc(sizeof(dma_addr_t) * n, GFP_KERNEL);
   if(!dma_addrs) {
      pr_err("%s: failed to allocate dma_addrs\n", __FUNCTION__);
      return -1;
   }

	char **mems  = (char**) kmalloc(sizeof(char*) * n, GFP_KERNEL);
   if(!mems) {
      pr_err("%s: failed to allocated mems\n", __FUNCTION__);
      kfree(dma_addrs);
      return -1;
   }

   for(ni=0; ni<n; ++ni) {
      mems[ni] = dma_alloc_coherent(d, size, &dma_addrs[ni], GFP_KERNEL);
      if(!mems[ni]) {
         pr_err("%s: Error dma_alloc_coherent\n", __FUNCTION__);
         r = -1;
         goto out;
      }
      iowrite32(dma_addrs[ni], mmio + KVM_TEST_DMA_MAP_ADDR);
      iowrite32(size, mmio + KVM_TEST_DMA_MAP_SIZE);
      iowrite32(0, mmio + KVM_TEST_DMA_MAP);
      pr_err("%s: coherent map @ p %llx, v %lx, s %d\n", __FUNCTION__, dma_addrs[ni], (unsigned long)mems[ni], size);
   }
   for(ni=0; ni<n; ++ni) {
      for(i=0; i<size; i+=(size/N_STEPS)) {

         mems[ni][i] = mems[ni][i] + 1;
      }
   }
   for(ni=0; ni<n; ++ni) {
      dma_free_coherent(d, size, mems[ni], dma_addrs[ni]);
      iowrite32(dma_addrs[ni], mmio + KVM_TEST_DMA_UNMAP_ADDR);
      iowrite32(0, mmio + KVM_TEST_DMA_UNMAP);
   }

out:
   kfree(mems);
   kfree(dma_addrs);
   return r;
}

static int stream_test1(struct device *d, unsigned int size)
{
   int i;
   int r=0;
	char* mem = (char*)kmalloc(size, GFP_KERNEL);
   if(!mem) {
      pr_err("%s: Error malloc\n", __FUNCTION__);
      return -1;
   }

   dma_addr_t dma_addr = dma_map_single(d, mem, size, PCI_DMA_FROMDEVICE);
   if (dma_mapping_error(d, dma_addr)) {
      pr_err("%s: Error dma_map_single\n", __FUNCTION__);
      r = -1;
      goto out;
   }
   iowrite32(dma_addr, mmio + KVM_TEST_DMA_MAP_ADDR);
   iowrite32(size, mmio + KVM_TEST_DMA_MAP_SIZE);
   iowrite32(0, mmio + KVM_TEST_DMA_MAP);
   pr_err("%s: streaming map @ p %llx, v %lx, s %d\n", __FUNCTION__, dma_addr, (unsigned long)mem, size);
   for(i=0; i<size; i+=(size/N_STEPS)) {
      mem[i] = mem[i] + 1;
   }
   pr_err("%s: streaming map @ p %llx, v %lx, s %d\n", __FUNCTION__, dma_addr, (unsigned long)mem, size);

   dma_unmap_single(d, dma_addr, size, PCI_DMA_FROMDEVICE);
   iowrite32(dma_addr, mmio + KVM_TEST_DMA_UNMAP_ADDR);
   iowrite32(0, mmio + KVM_TEST_DMA_UNMAP);
out:
   kfree(mem);
   pr_err("%s: Exit\n", __FUNCTION__);
   return r;
}

static int stream_free_overwrite(struct device *d)
{
   int i;
   int r=0;
   unsigned int size = 0x100;
	char* mem = (char*)kmalloc(0x1000, GFP_KERNEL);
   if(!mem) {
      pr_err("%s: Error malloc\n", __FUNCTION__);
      return -1;
   }

   dma_addr_t dma_addr = dma_map_single(d, mem, 0x100, PCI_DMA_FROMDEVICE);
   if (dma_mapping_error(d, dma_addr)) {
      pr_err("%s: Error dma_map_single\n", __FUNCTION__);
      r = -1;
      goto out;
   }
   iowrite32(dma_addr, mmio + KVM_TEST_DMA_MAP_ADDR);
   iowrite32(size, mmio + KVM_TEST_DMA_MAP_SIZE);
   iowrite32(0, mmio + KVM_TEST_DMA_MAP);
   pr_err("%s: streaming map @ p %llx, v %lx, s %d\n", __FUNCTION__, dma_addr, (unsigned long)mem, size);
   for(i=0; i<size; i+=(size/N_STEPS)) {
      mem[i] = mem[i] + 1;
   }
   dma_unmap_single(d, dma_addr, size, PCI_DMA_FROMDEVICE);
   iowrite32(dma_addr, mmio + KVM_TEST_DMA_UNMAP_ADDR);
   iowrite32(0, mmio + KVM_TEST_DMA_UNMAP);
   memset(mem, 0, 0x1000);
out:
   kfree(mem);
   return r;
}

static int stream_partial_test2(struct device *d, unsigned int size)
{
   int i;
   int r=0;
	char* mem = (char*)kmalloc(size * 2, GFP_KERNEL);
   if(!mem) {
      pr_err("%s: Error malloc\n", __FUNCTION__);
      return -1;
   }
   memset(mem, 0x42, size * 2);

   dma_addr_t dma_addr = dma_map_single(d, mem, size, PCI_DMA_FROMDEVICE);
   if (dma_mapping_error(d, dma_addr)) {
      pr_err("%s: Error dma_map_single\n", __FUNCTION__);
      r = 0;
      goto out;
   }
   iowrite32(dma_addr, mmio + KVM_TEST_DMA_MAP_ADDR);
   iowrite32(size, mmio + KVM_TEST_DMA_MAP_SIZE);
   iowrite32(0, mmio + KVM_TEST_DMA_MAP);
   pr_err("%s: streaming map @ p %llx, v %lx, s %d\n", __FUNCTION__, dma_addr, (unsigned long)mem, size);
   pr_err("%s: reading mapped dma\n", __FUNCTION__);
   for(i=0; i<size; i+=(size/N_STEPS)) {
      mem[i] = mem[i] + 1;
   }
   dma_unmap_single(d, dma_addr, size, PCI_DMA_FROMDEVICE);
   iowrite32(dma_addr, mmio + KVM_TEST_DMA_UNMAP_ADDR);
   iowrite32(0, mmio + KVM_TEST_DMA_UNMAP);
   pr_err("%s: reading un-mapped dma (should be fuzzed)\n", __FUNCTION__);
   // read -> this should be fuzzed
   for(i=0; i<size; i+=(size/N_STEPS)) {
      pr_err("mem[%d] = %x\n", i, mem[i]);
   }
   pr_err("%s: reading un-mapped dma (should not be fuzzed)\n", __FUNCTION__);
   // read again -> this should not be fuzzed
   for(i=0; i<size; i+=(size/N_STEPS)) {
      pr_err("mem[%d] = %x\n", i, mem[i]);
   }
   pr_err("%s: reading un-mapped dma (should be fuzzed)\n", __FUNCTION__);
   // read -> this should be fuzzed
   for(i=1; i<size; i+=(size/N_STEPS)) {
      pr_err("mem[%d] = %x\n", i, mem[i]);
   }
out:
   kfree(mem);
   pr_err("%s: Exit\n", __FUNCTION__);
   return r;
}

static int stream_partial_test3(struct device *d, unsigned int size)
{
   int i;
   int r=0;
	char* mem = (char*)kmalloc(size * 2, GFP_KERNEL);
   if(!mem) {
      pr_err("%s: Error malloc\n", __FUNCTION__);
      return -1;
   }
   memset(mem, 0x42, size * 2);

   dma_addr_t dma_addr = dma_map_single(d, mem, size, PCI_DMA_FROMDEVICE);
   if (dma_mapping_error(d, dma_addr)) {
      pr_err("%s: Error dma_map_single\n", __FUNCTION__);
      r = 0;
      goto out;
   }
   iowrite32(dma_addr, mmio + KVM_TEST_DMA_MAP_ADDR);
   iowrite32(size, mmio + KVM_TEST_DMA_MAP_SIZE);
   iowrite32(0, mmio + KVM_TEST_DMA_MAP);
   pr_err("%s: streaming map @ p %llx, v %lx, s %d\n", __FUNCTION__, dma_addr, (unsigned long)mem, size);
   pr_err("%s: reading mapped dma\n", __FUNCTION__);
   for(i=0; i<size; i+=(size/N_STEPS)) {
      mem[i] = mem[i] + 1;
   }
   dma_unmap_single(d, dma_addr, size, PCI_DMA_FROMDEVICE);
   iowrite32(dma_addr, mmio + KVM_TEST_DMA_UNMAP_ADDR);
   iowrite32(0, mmio + KVM_TEST_DMA_UNMAP);
   pr_err("%s: reading un-mapped dma (should be fuzzed)\n", __FUNCTION__);
   // read -> this should be fuzzed
   for(i=0; i<size; i+=(size/N_STEPS)) {
      pr_err("mem[%d] = %x\n", i, mem[i]);
   }
   pr_err("%s: writing un-mapped dma (should not be stored)\n", __FUNCTION__);
   // write -> this should be stored
   for(i=0; i<size; i+=(size/N_STEPS)) {
      mem[i] = 0x12;
   }
   pr_err("%s: reading un-mapped dma (should not be fuzzed)\n", __FUNCTION__);
   // read again -> this should not be fuzzed
   for(i=0; i<size; i+=(size/N_STEPS)) {
      pr_err("mem[%d] = %x\n", i, mem[i]);
   }
   pr_err("%s: reading un-mapped dma (should be fuzzed)\n", __FUNCTION__);
   // read -> this should be fuzzed
   for(i=1; i<size; i+=(size/N_STEPS)) {
      pr_err("mem[%d] = %x\n", i, mem[i]);
   }


out:
   kfree(mem);
   pr_err("%s: Exit\n", __FUNCTION__);
   return r;
}


static int stream_partial_test1(struct device *d, unsigned int size)
{
   int i;
   int r=0;
	char* mem = (char*)kmalloc(size * 2, GFP_KERNEL);
   if(!mem) {
      pr_err("%s: Error malloc\n", __FUNCTION__);
      return -1;
   }
   memset(mem, 0x42, size * 2);
   for(i=size; i<size*2; i+=(size/N_STEPS)) {
      pr_err("mem[%d]: %x\n", i, mem[i]);
      mem[i] = mem[i] + 1;
      pr_err("mem[%d]: %x\n", i, mem[i]);
   }

   dma_addr_t dma_addr = dma_map_single(d, mem, size, PCI_DMA_FROMDEVICE);
   if (dma_mapping_error(d, dma_addr)) {
      pr_err("%s: Error dma_map_single\n", __FUNCTION__);
      r = 0;
      goto out;
   }
   iowrite32(dma_addr, mmio + KVM_TEST_DMA_MAP_ADDR);
   iowrite32(size, mmio + KVM_TEST_DMA_MAP_SIZE);
   iowrite32(0, mmio + KVM_TEST_DMA_MAP);
   pr_err("%s: streaming map @ p %llx, v %lx, s %d\n", __FUNCTION__, dma_addr, (unsigned long)mem, size);
   for(i=0; i<size; i+=(size/N_STEPS)) {
      mem[i] = mem[i] + 1;
   }
   for(i=size; i<size*2; i+=(size/N_STEPS)) {
      pr_err("mem[%d]: %x\n", i, mem[i]);
      mem[i] = mem[i] + 1;
      pr_err("mem[%d]: %x\n", i, mem[i]);
   }
   dma_unmap_single(d, dma_addr, size, PCI_DMA_FROMDEVICE);
   iowrite32(dma_addr, mmio + KVM_TEST_DMA_UNMAP_ADDR);
   iowrite32(0, mmio + KVM_TEST_DMA_UNMAP);
out:
   kfree(mem);
   return r;
}

static int stream_testn(struct device *d, unsigned int size, unsigned int n)
{
   int ni, i;
   int r = 0;

   dma_addr_t *dma_addrs = (dma_addr_t*)kmalloc(sizeof(dma_addr_t) * n, GFP_KERNEL);
   if(!dma_addrs) {
      pr_err("%s: failed to allocate dma_addrs\n", __FUNCTION__);
      return -1;
   }

	char **mems  = (char**) kmalloc(sizeof(char*) * n, GFP_KERNEL);
   if(!mems) {
      pr_err("%s: failed to allocated mems\n", __FUNCTION__);
      kfree(dma_addrs);
      return -1;
   }

   for(ni=0; ni<n; ++ni) {
      dma_addrs[ni] = dma_map_single(d, mems[ni], size, PCI_DMA_FROMDEVICE);
      if (dma_mapping_error(d, dma_addrs[ni])) {
         pr_err("%s: Error dma_map_single\n", __FUNCTION__);
         r = -1;
         goto out;
      }
      iowrite32(dma_addrs[ni], mmio + KVM_TEST_DMA_MAP_ADDR);
      iowrite32(size, mmio + KVM_TEST_DMA_MAP_SIZE);
      iowrite32(0, mmio + KVM_TEST_DMA_MAP);
      pr_err("%s: stream map @ p %llx, v %lx, s %d\n", __FUNCTION__, dma_addrs[ni], (unsigned long)mems[ni], size);
   }
   for(ni=0; ni<n; ++ni) {
      for(i=0; i<size; i+=(size/N_STEPS)) {
         mems[ni][i] = mems[ni][i] + 1;
      }
   }
   for(ni=0; ni<n; ++ni) {
      dma_unmap_single(d, dma_addrs[ni], size, PCI_DMA_FROMDEVICE);
      iowrite32(dma_addrs[ni], mmio + KVM_TEST_DMA_UNMAP_ADDR);
      iowrite32(0, mmio + KVM_TEST_DMA_UNMAP);
   }

out:
   kfree(mems);
   kfree(dma_addrs);
   return r;
}


static irqreturn_t irq_handler(int irq, void *dev)
{
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
    pr_err("%s: successfull\n", __FUNCTION__);

    // partial page
    coherent_test1(&pdev->dev, 256);
    // multi page
    coherent_test1(&pdev->dev, 0x1256);
    // mutli allocs
    coherent_testn(&pdev->dev, 0x400, 5);
    // streaming multi page
    stream_test1(&pdev->dev, 0x1256);
    // streaming multi allocs
    //stream_testn(&pdev->dev, 0x400, 5);
    // streaming dma test with partial dma map access
    stream_partial_test3(&pdev->dev, 256);
    stream_partial_test2(&pdev->dev, 256);
    stream_free_overwrite(&pdev->dev);
    // streaming partial page
    // this will crash because kernel structures are colocated
    //stream_test1(&pdev->dev, 256);
    stream_test1(&pdev->dev, 512);
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

static struct pci_driver pci_driver = {
    .name     = CDEV_NAME,
    .id_table = id_table,
    .probe    = probe,
    .remove   = remove,
};

static int myinit(void)
{
    if (pci_register_driver(&pci_driver) < 0) {
        pr_err("%s Error\n", __FUNCTION__);
        return 1;
    }

    return 0;
}

static void myexit(void)
{
    pci_unregister_driver(&pci_driver);
}

rootfs_initcall(myinit);
module_exit(myexit);
