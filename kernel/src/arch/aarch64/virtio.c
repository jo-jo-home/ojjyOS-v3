/*
 * ojjyOS v3 Kernel - virtio-mmio Transport (aarch64)
 *
 * ojjyOS - Created by Jonas Lee
 */

#include "virtio.h"
#include "virtio_devices.h"
#include "serial.h"
#include "string.h"

#ifndef OJJY_HOST_TEST
static inline volatile uint32_t *vio_reg(uint32_t base, uint32_t off)
{
    return (volatile uint32_t *)(uintptr_t)(base + off);
}
#else
uint32_t virtio_host_mmio_read(uint32_t base, uint32_t off);
void virtio_host_mmio_write(uint32_t base, uint32_t off, uint32_t value);
#endif

static inline uint32_t vio_read(uint32_t base, uint32_t off)
{
#ifdef OJJY_HOST_TEST
    return virtio_host_mmio_read(base, off);
#else
    return *vio_reg(base, off);
#endif
}

static inline void vio_write(uint32_t base, uint32_t off, uint32_t value)
{
#ifdef OJJY_HOST_TEST
    virtio_host_mmio_write(base, off, value);
#else
    *vio_reg(base, off) = value;
#endif
}

/*
 * Modern (v2) device reset + init up to (not including) DRIVER_OK.
 * Negotiates no optional features, which every QEMU device accepts.
 * Returns 0 on success.
 */
static int virtio_dev_start(uint32_t base, uint32_t features)
{
    vio_write(base, VIO_STATUS, 0);                     /* Reset */
    vio_write(base, VIO_STATUS, VIO_STATUS_ACKNOWLEDGE | VIO_STATUS_DRIVER);

    /* Selector 0: no optional device-specific features.
     * Selector 1: negotiate VIRTIO_F_VERSION_1 (bit 32 = word 1 bit 0),
     * required by VirtIO 1.0+ (virtio-mmio version 2). */
    vio_write(base, VIO_DRVFEAT_SEL, 0);
    vio_write(base, VIO_DRVFEAT, features);
    vio_write(base, VIO_DRVFEAT_SEL, 1);
    vio_write(base, VIO_DRVFEAT, 1u);

    vio_write(base, VIO_STATUS,
        VIO_STATUS_ACKNOWLEDGE | VIO_STATUS_DRIVER | VIO_STATUS_FEATURES_OK);

    uint32_t status = vio_read(base, VIO_STATUS);
    if (!(status & VIO_STATUS_FEATURES_OK)) {
        serial_printf("[VIRTIO] Device at 0x%x rejected features\n", base);
        return -1;
    }
    return 0;
}

int virtio_queue_init(VirtQueue *vq, uint32_t mmio_base, VirtQueueMem *mem)
{
    memset(mem, 0, sizeof(*mem));

    vq->mem = mem;
    vq->free_head = 0;
    vq->avail_idx = 0;
    vq->used_idx = 0;
    vq->mmio = mmio_base;

    vio_write(mmio_base, VIO_QUEUE_SEL, 0);
    uint32_t max = vio_read(mmio_base, VIO_QUEUE_NUM_MAX);
    if (max == 0) {
        return -1;
    }

    /* Our static queue memory supports VQ_SIZE descriptors */
    /* Storage capacity is 128, but QEMU input commonly offers only 64.
     * Every ring index must wrap at this negotiated size. */
    uint16_t size = VQ_SIZE;
    while (size > max) size >>= 1;
    vq->size = size;
    vio_write(mmio_base, VIO_QUEUE_NUM, size);

    /* Identity-mapped: virtual == physical */
    uint64_t desc_addr = (uint64_t)(uintptr_t)&mem->desc[0];
    uint64_t avail_addr = (uint64_t)(uintptr_t)&mem->avail;
    uint64_t used_addr = (uint64_t)(uintptr_t)&mem->used;

    vio_write(mmio_base, VIO_QUEUE_DESC_LOW, (uint32_t)desc_addr);
    vio_write(mmio_base, VIO_QUEUE_DESC_HIGH, (uint32_t)(desc_addr >> 32));
    vio_write(mmio_base, VIO_QUEUE_AVAIL_LOW, (uint32_t)avail_addr);
    vio_write(mmio_base, VIO_QUEUE_AVAIL_HIGH, (uint32_t)(avail_addr >> 32));
    vio_write(mmio_base, VIO_QUEUE_USED_LOW, (uint32_t)used_addr);
    vio_write(mmio_base, VIO_QUEUE_USED_HIGH, (uint32_t)(used_addr >> 32));

    vio_write(mmio_base, VIO_QUEUE_READY, 1);
    return 0;
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
    vio_write(vq->mmio, VIO_QUEUE_NOTIFY, 0);
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

        vio_write(base, VIO_DEVFEAT_SEL, 0);
        uint32_t features = dev_id == VIRTIO_ID_BLOCK ?
            (vio_read(base, VIO_DEVFEAT) & (1u << 9)) : 0; /* BLK_F_FLUSH */
        if (virtio_dev_start(base, features) != 0) {
            continue;
        }

        switch (dev_id) {
            case VIRTIO_ID_BLOCK:
                if (virtio_blk_attach(base, features != 0) == 0) found++;
                break;
            case VIRTIO_ID_INPUT:
                if (virtio_input_attach(base) == 0) found++;
                break;
            default:
                serial_printf("[VIRTIO] Slot %d: device id %d not supported\n",
                    i, (int)dev_id);
                break;
        }
    }

    serial_printf("[VIRTIO] %d virtio device(s) attached\n", found);
}

/*
 * Helper for device drivers: finalize device bring-up (DRIVER_OK).
 */
void virtio_dev_ready(uint32_t base)
{
    uint32_t status = vio_read(base, VIO_STATUS);
    vio_write(base, VIO_STATUS, status | VIO_STATUS_DRIVER_OK);
}
