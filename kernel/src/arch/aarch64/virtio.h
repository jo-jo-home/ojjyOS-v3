/*
 * ojjyOS v3 Kernel - virtio Transport (aarch64)
 *
 * Two transports, one device handle:
 *   - virtio-mmio v2 ("modern") on the QEMU virt machine (32 fixed slots)
 *   - virtio-pci modern (capability walk, VirtIO 1.0 spec 4.1) on Apple's
 *     Virtualization framework (PCIe ECAM, firmware-assigned BARs)
 *
 * Every MMIO access is a single LDR/STR through mmio.h.
 *
 * ojjyOS - Created by Jonas Lee
 */

#ifndef _OJJY_ARCH_VIRTIO_H
#define _OJJY_ARCH_VIRTIO_H

#include "types.h"

struct PciDev;

static inline void virtio_memory_barrier(void)
{
#ifdef OJJY_HOST_TEST
    barrier();
#else
    __asm__ volatile("dmb ish" ::: "memory");
#endif
}

/* Wall-clock milliseconds from the ARM generic counter. Device waits use
 * this, not loop counts: under hardware virtualization (HVF) the CPU spins
 * far faster than QEMU's I/O thread completes requests, so a fixed spin
 * budget timed out real disk writes. Works with interrupts masked. */
static inline uint64_t virtio_now_ms(void)
{
#ifdef OJJY_HOST_TEST
    static uint64_t fake;
    return fake++;
#else
    uint64_t cnt, frq;
    __asm__ volatile("isb; mrs %0, cntvct_el0" : "=r"(cnt));
    __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(frq));
    return frq ? cnt / (frq / 1000) : 0;
#endif
}

/* virtio-mmio slot region on the QEMU virt machine */
#define VIRTIO_MMIO_BASE        0x0A000000UL
#define VIRTIO_MMIO_STRIDE      0x200
#define VIRTIO_MMIO_SLOTS       32

/* Device register offsets (virtio-mmio v2 / "modern" per VirtIO spec 4.2.2) */
#define VIO_MAGIC               0x000   /* 0x74726976 "virt" */
#define VIO_VERSION             0x004   /* 2 = modern */
#define VIO_DEVICE_ID           0x008
#define VIO_VENDOR_ID           0x00C
#define VIO_DEVFEAT             0x010
#define VIO_DEVFEAT_SEL         0x014
#define VIO_DRVFEAT             0x020
#define VIO_DRVFEAT_SEL         0x024
#define VIO_QUEUE_SEL           0x030
#define VIO_QUEUE_NUM_MAX       0x034
#define VIO_QUEUE_NUM           0x038
#define VIO_QUEUE_READY         0x044
#define VIO_QUEUE_NOTIFY        0x050
#define VIO_INT_STATUS          0x060
#define VIO_INT_ACK             0x064
#define VIO_STATUS              0x070
#define VIO_QUEUE_DESC_LOW      0x080
#define VIO_QUEUE_DESC_HIGH     0x084
#define VIO_QUEUE_AVAIL_LOW    0x090
#define VIO_QUEUE_AVAIL_HIGH    0x094
#define VIO_QUEUE_USED_LOW     0x0A0
#define VIO_QUEUE_USED_HIGH    0x0A4
#define VIO_CFG                 0x100

/* Device status bits (shared by both transports) */
#define VIO_STATUS_ACKNOWLEDGE  0x01
#define VIO_STATUS_DRIVER       0x02
#define VIO_STATUS_DRIVER_OK    0x04
#define VIO_STATUS_FEATURES_OK  0x08
#define VIO_STATUS_FAILED       0x80

/* virtio device IDs */
#define VIRTIO_ID_NET           1
#define VIRTIO_ID_BLOCK         2
#define VIRTIO_ID_CONSOLE       3
#define VIRTIO_ID_RNG           4
#define VIRTIO_ID_GPU           16
#define VIRTIO_ID_INPUT         18

/* Descriptor flags */
#define VRING_DESC_F_NEXT       1
#define VRING_DESC_F_WRITE      2

/*
 * Split virtqueue layout (single queue per device is enough here).
 */
#define VQ_SIZE                128     /* must be a power of two */

typedef struct {
    uint64_t addr;
    uint32_t len;
    uint16_t flags;
    uint16_t next;
} PACKED VringDesc;

typedef struct {
    uint16_t flags;
    uint16_t idx;
    uint16_t ring[VQ_SIZE];
    uint16_t used_event;
} PACKED VringAvail;          /* 2-byte aligned, follows desc (16-mult) */

typedef struct {
    uint32_t id;
    uint32_t len;
} PACKED VringUsedElem;

typedef struct {
    uint16_t flags;
    uint16_t idx;
    VringUsedElem elems[VQ_SIZE];
    uint16_t avail_event;
} VringUsed;

/*
 * Queue memory: desc (16-aligned) + avail + pad + used (4-aligned)
 */
typedef struct {
    VringDesc desc[VQ_SIZE] __attribute__((aligned(16)));
    VringAvail avail;
    uint8_t pad[2];            /* bring `used` to a 4-byte boundary */
    VringUsed used;
} VirtQueueMem;

/* Transport kinds */
#define VIRTIO_TRANSPORT_MMIO   0
#define VIRTIO_TRANSPORT_PCI   1

/*
 * One virtio device, transport-independent. The PCI fields hold absolute
 * addresses after BAR decoding (common/notify/isr/device-config regions).
 */
typedef struct VirtioDev {
    uint8_t kind;              /* VIRTIO_TRANSPORT_* */
    uint32_t mmio;             /* MMIO transport: slot base */
    uint16_t device_id;        /* virtio device id */
    /* PCI modern transport (absolute addresses) */
    uintptr_t common;          /* common configuration */
    uintptr_t notify_base;     /* notify capability BAR base */
    uintptr_t notify_cap;      /* capability offset within the notify BAR */
    uint64_t notify_mult;      /* notify_off_multiplier */
    uintptr_t isr;             /* ISR status */
    uintptr_t devcfg;          /* device-specific configuration */
    uint16_t pci_vendor, pci_device;
    bool attached;            /* early console must survive the later bus scan */
    uint32_t features;        /* negotiated device-specific feature word */
} VirtioDev;

typedef struct {
    VirtQueueMem *mem;
    uint16_t free_head;
    uint16_t avail_idx;        /* our next available index */
    uint16_t used_idx;         /* last processed used index */
    uint16_t size;             /* negotiated ring size, not storage capacity */
    VirtioDev *dev;            /* owning device (notify path) */
    uint16_t index;            /* queue number (net: 0 = RX, 1 = TX) */
    uintptr_t notify_addr;     /* resolved notify address (PCI transport) */
} VirtQueue;

/* --- bus scans (attach drivers to every device they know) --- */
void virtio_mmio_init(void);   /* QEMU virt */
void virtio_pci_init(void);    /* Apple VZ: PCI bus 0 */
void virtio_pci_console_init(void); /* boot-log sink before GPU/GIC bring-up */

/* Stable VirtioDev handles. virtio_dev_mmio_slot() returns the same handle
 * for the same slot on every call (so the early GPU probe and the later bus
 * scan agree); returns NULL when the table is full. */
VirtioDev *virtio_dev_mmio_slot(uint32_t base);
int virtio_dev_pci_setup(const struct PciDev *pci, VirtioDev **out);

/* --- transport API (works on both) --- */
/* Reset + feature negotiation up to (not including) DRIVER_OK. 0 on success.
 * features is a supported mask; dev->features records its offered subset. */
int virtio_dev_negotiate(VirtioDev *dev, uint32_t features);
int virtio_dev_begin(VirtioDev *dev, uint32_t features);   /* alias */
void virtio_dev_ready(VirtioDev *dev);                      /* set DRIVER_OK */
/* Queue setup: negotiate size, publish desc/avail/used, enable. 0 on success. */
int virtio_dev_queue_init(VirtioDev *dev, VirtQueue *vq, VirtQueueMem *mem, uint16_t index);
/* Device-offered feature word. Prefer negotiation's post-reset snapshot. */
uint32_t virtio_dev_offered(const VirtioDev *dev, uint32_t word);
/* Number of virtqueues the device exposes (0 on virtio-mmio). */
uint32_t virtio_dev_num_queues(const VirtioDev *dev);
/* Device-specific config region. */
uint8_t  virtio_dev_cfg8(const VirtioDev *dev, uint32_t off);
uint16_t virtio_dev_cfg16(const VirtioDev *dev, uint32_t off);
uint32_t virtio_dev_cfg32(const VirtioDev *dev, uint32_t off);
void virtio_dev_cfg_write8(const VirtioDev *dev, uint32_t off, uint8_t value);

/* --- virtqueue primitives (transport-independent) --- */
/* Tell the device that queue `vq` has new available buffers. */
void virtio_notify(const VirtQueue *vq);
/* Begin a descriptor chain: adds one descriptor, returns its index. */
uint16_t virtio_desc_add(VirtQueue *vq, uint64_t addr, uint32_t len, uint16_t flags);
/* Submit the chain (from head) to the available ring and notify. */
void virtio_submit(VirtQueue *vq, uint16_t head);
/* Poll for a used-ring advance. 1 when a chain completed (fills its length). */
int virtio_poll_used(VirtQueue *vq, uint32_t *out_len);

#endif /* _OJJY_ARCH_VIRTIO_H */
