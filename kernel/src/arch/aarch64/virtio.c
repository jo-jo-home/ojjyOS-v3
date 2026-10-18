/*
 * ojjyOS v3 Kernel - virtio Transport (see virtio.h)
 *
 * ojjyOS - Created by Jonas Lee
 */

#include "virtio.h"
#include "virtio_devices.h"
#include "serial.h"
#include "string.h"
#include "mmio.h"

#ifndef OJJY_HOST_TEST
#include "pci.h"
#include "platform.h"
#include "arch_power.h"
#endif

#ifdef OJJY_HOST_TEST
uint32_t virtio_host_mmio_read(uint32_t base, uint32_t off);
void virtio_host_mmio_write(uint32_t base, uint32_t off, uint32_t value);
#endif

static inline uint32_t vio_read(uint32_t base, uint32_t off)
{
#ifdef OJJY_HOST_TEST
    return virtio_host_mmio_read(base, off);
#else
    return mmio_read32((uintptr_t)base + off);
#endif
}

static inline void vio_write(uint32_t base, uint32_t off, uint32_t value)
{
#ifdef OJJY_HOST_TEST
    virtio_host_mmio_write(base, off, value);
#else
    mmio_write32((uintptr_t)base + off, value);
#endif
}

/*
 * Stable device handles: drivers keep VirtQueue.dev pointers into this
 * table, so entries must never move once handed out.
 */
#define MAX_VIRTIO_DEVS 16
static VirtioDev virtio_devs[MAX_VIRTIO_DEVS];
static unsigned virtio_dev_count;

static VirtioDev *virtio_dev_new(void)
{
    if (virtio_dev_count >= MAX_VIRTIO_DEVS) return NULL;
    VirtioDev *d = &virtio_devs[virtio_dev_count++];
    memset(d, 0, sizeof(*d));
    return d;
}

VirtioDev *virtio_dev_mmio_slot(uint32_t base)
{
    for (unsigned i = 0; i < virtio_dev_count; i++) {
        if (virtio_devs[i].kind == VIRTIO_TRANSPORT_MMIO && virtio_devs[i].mmio == base) {
            return &virtio_devs[i];
        }
    }
    VirtioDev *d = virtio_dev_new();
    if (!d) return NULL;
    d->kind = VIRTIO_TRANSPORT_MMIO;
    d->mmio = base;
    return d;
}

/* ------------------------------------------------------------------ */
/* virtio-pci modern transport (VirtIO 1.0 spec 4.1). Host build only: */
/* the host ring tests exercise the virtqueue logic, not the bus.     */
/* ------------------------------------------------------------------ */
#ifndef OJJY_HOST_TEST

/* Common configuration offsets within the COMMON capability region. */
#define VPCI_DF_SEL        0x00    /* device_feature_select */
#define VPCI_DF            0x04    /* device_feature */
#define VPCI_DDF_SEL       0x08    /* driver_feature_select */
#define VPCI_DDF           0x0C    /* driver_feature */
#define VPCI_MSIX_CFG      0x10
#define VPCI_NUM_QUEUES    0x12
#define VPCI_STATUS        0x14    /* u8 */
#define VPCI_CFG_GEN       0x15
#define VPCI_Q_SEL         0x16
#define VPCI_Q_SIZE        0x18
#define VPCI_Q_MSIX        0x1A
#define VPCI_Q_ENABLE      0x1C
#define VPCI_Q_NOTIFY_OFF  0x1E
#define VPCI_Q_DESC        0x20    /* u64 */
#define VPCI_Q_DRIVER      0x28    /* u64 */
#define VPCI_Q_DEVICE      0x30    /* u64 */

static uint8_t pci_cfg8(const PciDev *p, uint16_t off)
{
    return (uint8_t)(pci_config_read32(p->bus, p->dev, p->fn, (uint16_t)(off & ~3u)) >> ((off & 3) * 8));
}

int virtio_dev_pci_setup(const PciDev *pci, VirtioDev **out)
{
    /* Same device (vendor:id) already set up: return the same handle. */
    for (unsigned i = 0; i < virtio_dev_count; i++) {
        if (virtio_devs[i].kind == VIRTIO_TRANSPORT_PCI &&
            virtio_devs[i].pci_vendor == pci->vendor &&
            virtio_devs[i].pci_device == pci->device) {
            if (out) *out = &virtio_devs[i];
            return 0;
        }
    }

    VirtioDev *d = virtio_dev_new();
    if (!d) return -1;
    d->kind = VIRTIO_TRANSPORT_PCI;
    d->pci_vendor = pci->vendor;
    d->pci_device = pci->device;
    d->device_id = (uint16_t)(pci->device - 0x1040);   /* modern layout */

    /* Walk the vendor capability list (PCI status bit 4, pointer at 0x34). */
    uint32_t status = pci_config_read32(pci->bus, pci->dev, pci->fn, 4) >> 16;
    if (!(status & 0x10)) goto fail;
    uint8_t cap = pci_cfg8(pci, 0x34);
    for (int guard = 0; cap != 0 && guard < 64; guard++) {
        uint8_t vndr = pci_cfg8(pci, cap);
        uint8_t next = pci_cfg8(pci, (uint16_t)(cap + 1));
        if (vndr == 9) {                                /* vendor-specific */
            uint8_t cfg_type = pci_cfg8(pci, (uint16_t)(cap + 3));
            uint8_t bar = pci_cfg8(pci, (uint16_t)(cap + 4));
            uint32_t off = pci_config_read32(pci->bus, pci->dev, pci->fn, (uint16_t)(cap + 8));
            if (bar < 6 && pci->bars[bar]) {
                switch (cfg_type) {
                case 1: d->common = pci->bars[bar] + off; break;      /* COMMON */
                case 2:                                             /* NOTIFY */
                    d->notify_base = pci->bars[bar];
                    d->notify_cap = off;
                    d->notify_mult = pci_config_read32(pci->bus, pci->dev, pci->fn, (uint16_t)(cap + 16));
                    break;
                case 3: d->isr = pci->bars[bar] + off; break;        /* ISR */
                case 4: d->devcfg = pci->bars[bar] + off; break;     /* DEVICE */
                default: break;
                }
            }
        }
        cap = next;
    }
    if (!d->common || !d->notify_base || !d->devcfg) goto fail;
    if (out) *out = d;
    return 0;
fail:
    /* Release the table slot again: this device is unusable. */
    virtio_dev_count--;
    return -1;
}

static int pci_negotiate(VirtioDev *d, uint32_t features)
{
    mmio_write8(d->common + VPCI_STATUS, 0);
    mmio_write8(d->common + VPCI_STATUS, VIO_STATUS_ACKNOWLEDGE | VIO_STATUS_DRIVER);
    mmio_write32(d->common + VPCI_DDF_SEL, 0);
    mmio_write32(d->common + VPCI_DDF, features);
    /* Word 1 bit 0 = VIRTIO_F_VERSION_1: mandatory for modern PCI. */
    mmio_write32(d->common + VPCI_DDF_SEL, 1);
    mmio_write32(d->common + VPCI_DDF, 1u);
    mmio_write8(d->common + VPCI_STATUS,
                VIO_STATUS_ACKNOWLEDGE | VIO_STATUS_DRIVER | VIO_STATUS_FEATURES_OK);
    if (!(mmio_read8(d->common + VPCI_STATUS) & VIO_STATUS_FEATURES_OK)) {
        serial_printf("[VIRTIO-PCI] %x:%x rejected features\n",
            (unsigned int)d->pci_vendor, (unsigned int)d->pci_device);
        return -1;
    }
    return 0;
}

static int pci_queue_init(VirtioDev *d, VirtQueue *vq, VirtQueueMem *mem, uint16_t index)
{
    memset(mem, 0, sizeof(*mem));
    vq->mem = mem;
    vq->free_head = 0;
    vq->avail_idx = 0;
    vq->used_idx = 0;
    vq->dev = d;
    vq->index = index;

    mmio_write16(d->common + VPCI_Q_SEL, index);
    uint32_t max = mmio_read16(d->common + VPCI_Q_SIZE);
    if (max == 0) return -1;

    uint16_t size = VQ_SIZE;
    while (size > max) size >>= 1;
    vq->size = size;
    mmio_write16(d->common + VPCI_Q_SIZE, size);
    mmio_write16(d->common + VPCI_Q_MSIX, 0xFFFF);      /* no MSI-X: we poll */

    /* Identity-mapped: virtual == physical. */
    uint64_t desc_addr = (uint64_t)(uintptr_t)&mem->desc[0];
    uint64_t avail_addr = (uint64_t)(uintptr_t)&mem->avail;
    uint64_t used_addr = (uint64_t)(uintptr_t)&mem->used;
    mmio_write32(d->common + VPCI_Q_DESC, (uint32_t)desc_addr);
    mmio_write32(d->common + VPCI_Q_DESC + 4, (uint32_t)(desc_addr >> 32));
    mmio_write32(d->common + VPCI_Q_DRIVER, (uint32_t)avail_addr);
    mmio_write32(d->common + VPCI_Q_DRIVER + 4, (uint32_t)(avail_addr >> 32));
    mmio_write32(d->common + VPCI_Q_DEVICE, (uint32_t)used_addr);
    mmio_write32(d->common + VPCI_Q_DEVICE + 4, (uint32_t)(used_addr >> 32));

    uint16_t notify_off = mmio_read16(d->common + VPCI_Q_NOTIFY_OFF);
    vq->notify_addr = d->notify_base + d->notify_cap +
                      (uint64_t)notify_off * d->notify_mult;

    mmio_write16(d->common + VPCI_Q_ENABLE, 1);
    return 0;
}

#endif /* !OJJY_HOST_TEST */

/* ------------------------------------------------------------------ */
/* virtio-mmio transport                                              */
/* ------------------------------------------------------------------ */

static int mmio_negotiate(VirtioDev *d, uint32_t features)
{
    vio_write(d->mmio, VIO_STATUS, 0);                  /* Reset */
    vio_write(d->mmio, VIO_STATUS, VIO_STATUS_ACKNOWLEDGE | VIO_STATUS_DRIVER);

    /* Selector 0: device-specific features (flush/mac/...).
     * Selector 1: negotiate VIRTIO_F_VERSION_1 (word 1 bit 0), required
     * by VirtIO 1.0+ (virtio-mmio version 2). */
    vio_write(d->mmio, VIO_DRVFEAT_SEL, 0);
    vio_write(d->mmio, VIO_DRVFEAT, features);
    vio_write(d->mmio, VIO_DRVFEAT_SEL, 1);
    vio_write(d->mmio, VIO_DRVFEAT, 1u);

    vio_write(d->mmio, VIO_STATUS,
        VIO_STATUS_ACKNOWLEDGE | VIO_STATUS_DRIVER | VIO_STATUS_FEATURES_OK);

    if (!(vio_read(d->mmio, VIO_STATUS) & VIO_STATUS_FEATURES_OK)) {
        serial_printf("[VIRTIO] Device at 0x%x rejected features\n",
            (unsigned int)d->mmio);
        return -1;
    }
    return 0;
}

static int mmio_queue_init(VirtioDev *d, VirtQueue *vq, VirtQueueMem *mem, uint16_t index)
{
    memset(mem, 0, sizeof(*mem));
    vq->mem = mem;
    vq->free_head = 0;
    vq->avail_idx = 0;
    vq->used_idx = 0;
    vq->dev = d;
    vq->index = index;

    vio_write(d->mmio, VIO_QUEUE_SEL, index);
    uint32_t max = vio_read(d->mmio, VIO_QUEUE_NUM_MAX);
    if (max == 0) {
        return -1;
    }

    /* Storage capacity is 128, but devices commonly offer less (QEMU input
     * offers 64). Every ring index must wrap at the negotiated size. */
    uint16_t size = VQ_SIZE;
    while (size > max) size >>= 1;
    vq->size = size;
    vio_write(d->mmio, VIO_QUEUE_NUM, size);

    /* Identity-mapped: virtual == physical */
    uint64_t desc_addr = (uint64_t)(uintptr_t)&mem->desc[0];
    uint64_t avail_addr = (uint64_t)(uintptr_t)&mem->avail;
    uint64_t used_addr = (uint64_t)(uintptr_t)&mem->used;

    vio_write(d->mmio, VIO_QUEUE_DESC_LOW, (uint32_t)desc_addr);
    vio_write(d->mmio, VIO_QUEUE_DESC_HIGH, (uint32_t)(desc_addr >> 32));
    vio_write(d->mmio, VIO_QUEUE_AVAIL_LOW, (uint32_t)avail_addr);
    vio_write(d->mmio, VIO_QUEUE_AVAIL_HIGH, (uint32_t)(avail_addr >> 32));
    vio_write(d->mmio, VIO_QUEUE_USED_LOW, (uint32_t)used_addr);
    vio_write(d->mmio, VIO_QUEUE_USED_HIGH, (uint32_t)(used_addr >> 32));

    vio_write(d->mmio, VIO_QUEUE_READY, 1);
    return 0;
}

/* ------------------------------------------------------------------ */
/* Transport-independent API                                          */
/* ------------------------------------------------------------------ */

int virtio_dev_negotiate(VirtioDev *dev, uint32_t features)
{
#ifndef OJJY_HOST_TEST
    if (dev->kind == VIRTIO_TRANSPORT_PCI) return pci_negotiate(dev, features);
#endif
    return mmio_negotiate(dev, features);
}

int virtio_dev_begin(VirtioDev *dev, uint32_t features)
{
    return virtio_dev_negotiate(dev, features);
}

void virtio_dev_ready(VirtioDev *dev)
{
#ifndef OJJY_HOST_TEST
    if (dev->kind == VIRTIO_TRANSPORT_PCI) {
        uint8_t status = mmio_read8(dev->common + VPCI_STATUS);
        mmio_write8(dev->common + VPCI_STATUS, status | VIO_STATUS_DRIVER_OK);
        return;
    }
#endif
    uint32_t status = vio_read(dev->mmio, VIO_STATUS);
    vio_write(dev->mmio, VIO_STATUS, status | VIO_STATUS_DRIVER_OK);
}

int virtio_dev_queue_init(VirtioDev *dev, VirtQueue *vq, VirtQueueMem *mem, uint16_t index)
{
#ifndef OJJY_HOST_TEST
    if (dev->kind == VIRTIO_TRANSPORT_PCI) return pci_queue_init(dev, vq, mem, index);
#endif
    return mmio_queue_init(dev, vq, mem, index);
}

uint32_t virtio_dev_offered(const VirtioDev *dev, uint32_t word)
{
#ifndef OJJY_HOST_TEST
    if (dev->kind == VIRTIO_TRANSPORT_PCI) {
        mmio_write32(dev->common + VPCI_DF_SEL, word);
        return mmio_read32(dev->common + VPCI_DF);
    }
#endif
    vio_write(dev->mmio, VIO_DEVFEAT_SEL, word);
    return vio_read(dev->mmio, VIO_DEVFEAT);
}

/* Number of virtqueues the device exposes (0 on virtio-mmio). */
uint32_t virtio_dev_num_queues(const VirtioDev *dev)
{
#ifndef OJJY_HOST_TEST
    if (dev->kind == VIRTIO_TRANSPORT_PCI) return mmio_read16(dev->common + VPCI_NUM_QUEUES);
#endif
    return 0;
}

uint32_t virtio_dev_cfg32(const VirtioDev *dev, uint32_t off)
{
#ifndef OJJY_HOST_TEST
    if (dev->kind == VIRTIO_TRANSPORT_PCI) return mmio_read32(dev->devcfg + off);
#endif
    return vio_read(dev->mmio, VIO_CFG + off);
}

uint16_t virtio_dev_cfg16(const VirtioDev *dev, uint32_t off)
{
#ifndef OJJY_HOST_TEST
    if (dev->kind == VIRTIO_TRANSPORT_PCI) return mmio_read16(dev->devcfg + off);
    return mmio_read16((uintptr_t)dev->mmio + VIO_CFG + off);
#else
    (void)off;
    return (uint16_t)vio_read(dev->mmio, VIO_CFG);
#endif
}

uint8_t virtio_dev_cfg8(const VirtioDev *dev, uint32_t off)
{
#ifndef OJJY_HOST_TEST
    if (dev->kind == VIRTIO_TRANSPORT_PCI) return mmio_read8(dev->devcfg + off);
    /* Byte access: the register may not be 4-byte aligned (input config). */
    return mmio_read8((uintptr_t)dev->mmio + VIO_CFG + off);
#else
    (void)off;
    return (uint8_t)vio_read(dev->mmio, VIO_CFG);
#endif
}

void virtio_dev_cfg_write8(const VirtioDev *dev, uint32_t off, uint8_t value)
{
#ifndef OJJY_HOST_TEST
    if (dev->kind == VIRTIO_TRANSPORT_PCI) {
        mmio_write8(dev->devcfg + off, value);
        return;
    }
    /* Byte access: the virtio-mmio config select registers are byte-wide. */
    mmio_write8((uintptr_t)dev->mmio + VIO_CFG + off, value);
#else
    vio_write(dev->mmio, VIO_CFG + off, value);
#endif
}

/* ------------------------------------------------------------------ */
/* Virtqueue primitives                                              */
/* ------------------------------------------------------------------ */

void virtio_notify(const VirtQueue *vq)
{
    virtio_memory_barrier();
#ifndef OJJY_HOST_TEST
    if (vq->dev->kind == VIRTIO_TRANSPORT_PCI) {
        mmio_write16(vq->notify_addr, vq->index);
        return;
    }
#endif
    vio_write(vq->dev->mmio, VIO_QUEUE_NOTIFY, vq->index);
}

/*
 * Add one descriptor to the current chain.
 * The caller passes VRING_DESC_F_NEXT for every descriptor except
 * the last one of the chain. Chains are submitted one at a time in
 * this kernel, so the free descriptors are always contiguous.
 * Returns the descriptor index, or 0xFFFF when the table is full.
 */
uint16_t virtio_desc_add(VirtQueue *vq, uint64_t addr, uint32_t len, uint16_t flags)
{
    uint16_t idx = vq->free_head;
    if (idx >= vq->size) return 0xFFFF;

    VringDesc *d = &vq->mem->desc[idx];
    d->addr = addr;
    d->len = len;
    d->flags = flags;
    d->next = (flags & VRING_DESC_F_NEXT) ? (uint16_t)(idx + 1) : 0;

    if (idx + 1 < vq->size) {
        vq->free_head = (uint16_t)(idx + 1);
    } else {
        vq->free_head = 0xFFFF;    /* Table exhausted until recycle */
    }
    return idx;
}

void virtio_submit(VirtQueue *vq, uint16_t head)
{
    /* Publish the head descriptor in the available ring */
    uint16_t idx = vq->avail_idx;
    vq->mem->avail.ring[idx & (vq->size - 1)] = head;
    virtio_memory_barrier();     /* Ring before index */
    vq->mem->avail.idx = (uint16_t)(idx + 1);
    vq->avail_idx = (uint16_t)(idx + 1);
    virtio_memory_barrier();

    /* Kick the device */
    virtio_notify(vq);
}

int virtio_poll_used(VirtQueue *vq, uint32_t *out_len)
{
    uint16_t used_idx = *(volatile uint16_t *)&vq->mem->used.idx;
    if (used_idx == vq->used_idx) {
        return 0;
    }

    virtio_memory_barrier();

    VringUsedElem *elem = &vq->mem->used.elems[vq->used_idx & (vq->size - 1)];
    if (out_len) *out_len = elem->len;

    /* Recycle the chain: the descriptors were allocated contiguously
     * starting at the chain head, so the free head returns there.
     * (Valid because exactly one chain is in flight per queue.) */
    vq->free_head = (uint16_t)elem->id;

    vq->used_idx = (uint16_t)(vq->used_idx + 1);
    return 1;
}

/* ------------------------------------------------------------------ */
/* Bus scans                                                          */
/* ------------------------------------------------------------------ */

static int virtio_attach_one(VirtioDev *d)
{
    if (d->device_id == VIRTIO_ID_GPU) {
        /* Brought up early as the display; never reset it here. */
        return virtio_gpu_owns(d) ? 0 : -1;
    }

    uint32_t offered = virtio_dev_offered(d, 0);
    uint32_t features = 0;
    switch (d->device_id) {
    case VIRTIO_ID_BLOCK: features = offered & (1u << 9); break;  /* BLK_F_FLUSH */
    case VIRTIO_ID_NET:   features = offered & (1u << 5); break;  /* NET_F_MAC */
    default: break;
    }
    if (virtio_dev_negotiate(d, features) != 0) {
        return -1;
    }

    switch (d->device_id) {
    case VIRTIO_ID_BLOCK:
        return virtio_blk_attach(d, features != 0);
    case VIRTIO_ID_INPUT:
        return virtio_input_attach(d);
    case VIRTIO_ID_NET:
        return virtio_net_attach(d, features != 0);
    case VIRTIO_ID_RNG:
        return virtio_rng_attach(d);
    case VIRTIO_ID_CONSOLE:
        return virtio_console_attach(d);
    default:
        serial_printf("[VIRTIO] device id %d not supported\n", (int)d->device_id);
        return -1;
    }
}

void virtio_mmio_init(void)
{
    int found = 0;

    for (int i = 0; i < VIRTIO_MMIO_SLOTS; i++) {
        uint32_t base = VIRTIO_MMIO_BASE + (uint32_t)i * VIRTIO_MMIO_STRIDE;

        uint32_t magic = vio_read(base, VIO_MAGIC);
        if (magic != 0x74726976u) {     /* "virt" */
            continue;
        }

        uint32_t version = vio_read(base, VIO_VERSION);
        if (version != 2) {
            serial_printf("[VIRTIO] Slot %d: unsupported version %d\n",
                i, (int)version);
            continue;
        }

        uint32_t dev_id = vio_read(base, VIO_DEVICE_ID);
        if (dev_id == 0) {
            continue;     /* Empty slot */
        }

        VirtioDev *d = virtio_dev_mmio_slot(base);
        if (!d) break;
        d->device_id = (uint16_t)dev_id;

        if (virtio_attach_one(d) == 0) found++;
    }

    serial_printf("[VIRTIO] %d virtio device(s) attached\n", found);
}

#ifndef OJJY_HOST_TEST
void virtio_pci_init(void)
{
    if (!platform.ecam) return;

    /* Modern virtio PCI device ids are 0x1040 + virtio id. */
    static const uint16_t pci_ids[] = {
        0x1041,     /* net */
        0x1042,     /* block */
        0x1043,     /* console */
        0x1044,     /* rng */
        0x1050,     /* gpu */
    };
    int found = 0;

    for (unsigned i = 0; i < sizeof(pci_ids) / sizeof(pci_ids[0]); i++) {
        PciDev pci;
        if (pci_find(0x1af4, pci_ids[i], &pci) != 0) continue;

        VirtioDev *d;
        if (virtio_dev_pci_setup(&pci, &d) != 0) continue;

        if (virtio_attach_one(d) == 0) found++;
    }

    serial_printf("[VIRTIO-PCI] %d virtio device(s) attached\n", found);
}
#endif
