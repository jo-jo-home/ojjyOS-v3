/*
 * ojjyOS v3 Kernel - virtio-mmio Transport (aarch64)
 *
 * Discovers virtio devices on the QEMU virt machine's virtio-mmio
 * bus (32 fixed MMIO slots) and provides a minimal split-virtqueue
 * implementation used by the input and block drivers.
 *
 * ojjyOS - Created by Jonas Lee
 */

#ifndef _OJJY_ARCH_VIRTIO_H
#define _OJJY_ARCH_VIRTIO_H

#include "types.h"

static inline void virtio_memory_barrier(void)
{
#ifdef OJJY_HOST_TEST
    barrier();
#else
    __asm__ volatile("dmb ish" ::: "memory");
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
#define VIO_QUEUE_AVAIL_LOW     0x090
#define VIO_QUEUE_AVAIL_HIGH    0x094
#define VIO_QUEUE_USED_LOW      0x0A0
#define VIO_QUEUE_USED_HIGH     0x0A4
#define VIO_CFG                 0x100

/* Device status bits */
#define VIO_STATUS_ACKNOWLEDGE  0x01
#define VIO_STATUS_DRIVER       0x02
#define VIO_STATUS_DRIVER_OK    0x04
#define VIO_STATUS_FEATURES_OK  0x08
#define VIO_STATUS_FAILED       0x80

/* virtio device IDs */
#define VIRTIO_ID_NET           1
#define VIRTIO_ID_BLOCK         2
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
} PACKED VringUsed;

/*
 * Queue memory: desc (16-aligned) + avail + pad + used (4-aligned)
 */
typedef struct {
    VringDesc desc[VQ_SIZE] __attribute__((aligned(16)));
    VringAvail avail;
    uint8_t pad[2];            /* bring `used` to a 4-byte boundary */
    VringUsed used;
} VirtQueueMem;

typedef struct {
    VirtQueueMem *mem;
    uint16_t free_head;
    uint16_t avail_idx;        /* our next available index */
    uint16_t used_idx;        /* last processed used index */
    uint16_t size;            /* negotiated ring size, not storage capacity */
    uint32_t mmio;            /* device MMIO base address */
    uint16_t index;           /* queue number (net: 0 = RX, 1 = TX) */
} VirtQueue;

/*
 * Probe the virtio-mmio bus and attach known devices.
 */
void virtio_mmio_init(void);

/*
 * Initialize a virtqueue on the device (queue 0) using the given
 * statically-allocated queue memory. Returns 0 on success.
 */
int virtio_queue_init(VirtQueue *vq, uint32_t mmio_base, VirtQueueMem *mem);
/* Same, for queue number `index` (multi-queue devices such as virtio-net). */
int virtio_queue_init_index(VirtQueue *vq, uint32_t mmio_base, VirtQueueMem *mem, uint16_t index);
/* Tell the device that queue `vq` has new available buffers. */
void virtio_notify(const VirtQueue *vq);

/*
 * Begin building a descriptor chain: adds one descriptor and returns
 * the head index of the chain.
 */
uint16_t virtio_desc_add(VirtQueue *vq, uint64_t addr, uint32_t len, uint16_t flags);

/*
 * Submit the current chain (from head) to the available ring and
 * notify the device.
 */
void virtio_submit(VirtQueue *vq, uint16_t head);

/*
 * Poll for a used-ring advance. Returns 1 when a new used element
 * arrived (and fills desc/len of the chain head), 0 otherwise.
 */
int virtio_poll_used(VirtQueue *vq, uint32_t *out_len);

#endif /* _OJJY_ARCH_VIRTIO_H */
