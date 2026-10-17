/*
 * ojjyOS v3 Kernel - virtio-blk Driver (aarch64)
 *
 * Block device over virtio-mmio. Implements the block device
 * operations used by the persistence layer (sector read/write).
 *
 * Requests are synchronous: submit one chain, poll the used ring
 * (the kernel is single-threaded), recycle the descriptors.
 *
 * ojjyOS - Created by Jonas Lee
 */

#include "virtio.h"
#include "virtio_devices.h"
#include "drivers/blockdev.h"
#include "serial.h"
#include "string.h"

#define VIRTIO_BLK_T_IN    0
#define VIRTIO_BLK_T_OUT   1

/* Capacity: #sectors as u64 in the device config at VIO_CFG + 0 */
#define VIRTIO_BLK_CFG_CAPACITY   0

typedef struct {
    uint32_t type;       /* VIRTIO_BLK_T_* */
    uint32_t reserved;
    uint64_t sector;     /* Start LBA */
} PACKED VirtioBlkReq;

typedef struct {
    VirtQueue vq;
    VirtQueueMem queue_mem;  /* Static queue memory for this device */
    VirtioBlkReq req;        /* Request header (device-readable) */
    uint8_t data[512];       /* Sector payload */
    uint8_t status;          /* Device-writable status byte */
} BlkDevState;

static BlkDevState blk_dev;
static bool blk_present = false;

int virtio_blk_attach(uint32_t mmio_base)
{
    if (blk_present) {
        return -1;   /* One block device is enough */
    }

    if (virtio_queue_init(&blk_dev.vq, mmio_base, &blk_dev.queue_mem) != 0) {
        return -1;
    }

    virtio_dev_ready(mmio_base);
    blk_present = true;

    /* Capacity: 8 bytes at config offset 0 (little-endian) */
    volatile uint32_t *cfg = (volatile uint32_t *)(uintptr_t)(mmio_base + VIO_CFG);
    uint64_t sectors = (uint64_t)cfg[0] | ((uint64_t)cfg[1] << 32);

    serial_printf("[VIRTIO-BLK] Attached: %d MB (%d sectors)\n",
        (int)(sectors * 512 / (1024 * 1024)), (int)sectors);
    return 0;
}

/*
 * One synchronous 512-byte sector transfer.
 */
static int blk_transfer_sector(uint64_t lba, void *buf, bool write)
{
    if (!blk_present) return -1;

    VirtQueue *vq = &blk_dev.vq;

    blk_dev.req.type = write ? VIRTIO_BLK_T_OUT : VIRTIO_BLK_T_IN;
    blk_dev.req.reserved = 0;
    blk_dev.req.sector = lba;
    blk_dev.status = 0xFF;    /* Sentinel: the device must set 0 */

    if (write) {
        memcpy(blk_dev.data, buf, 512);
    }

    uint64_t req_addr = (uint64_t)(uintptr_t)&blk_dev.req;
    uint64_t data_addr = (uint64_t)(uintptr_t)blk_dev.data;
    uint64_t status_addr = (uint64_t)(uintptr_t)&blk_dev.status;

    __asm__ volatile("dmb ish" ::: "memory");

    /* Chain: header -> data -> status.
     * The data buffer is device-writable for reads, device-readable
     * for writes. The status byte is always device-writable. */
    uint16_t d0 = virtio_desc_add(vq, req_addr, sizeof(VirtioBlkReq), VRING_DESC_F_NEXT);
    uint16_t d1 = virtio_desc_add(vq, data_addr, 512,
        write ? VRING_DESC_F_NEXT : (VRING_DESC_F_NEXT | VRING_DESC_F_WRITE));
    uint16_t d2 = virtio_desc_add(vq, status_addr, 1, VRING_DESC_F_WRITE);
    if (d0 == 0xFFFF || d1 == 0xFFFF || d2 == 0xFFFF) {
        return -1;
    }
    virtio_submit(vq, d0);

    /* Wait for completion (bounded) */
    uint32_t len = 0;
    int spins = 0;
    while (!virtio_poll_used(vq, &len)) {
        if (++spins > 5000000) {
            serial_printf("[VIRTIO-BLK] Timeout at sector %d\n", (int)lba);
            return -1;
        }
    }

    if (blk_dev.status != 0) {
        serial_printf("[VIRTIO-BLK] Error status %d at sector %d\n",
            blk_dev.status, (int)lba);
        return -1;
    }

    if (!write) {
        memcpy(buf, blk_dev.data, 512);
    }
    return 0;
}

/*
 * Block device API implementation (aarch64 side).
 */
int block_read_sectors(uint64_t lba, void *buf, uint32_t count)
{
    if (!buf || count == 0) return -1;
    for (uint32_t s = 0; s < count; s++) {
        if (blk_transfer_sector(lba + s, (uint8_t *)buf + s * 512, false) != 0) {
            return -1;
        }
    }
    return 0;
}

int block_write_sectors(uint64_t lba, const void *buf, uint32_t count)
{
    if (!buf || count == 0) return -1;
    for (uint32_t s = 0; s < count; s++) {
        if (blk_transfer_sector(lba + s, (void *)((const uint8_t *)buf + s * 512), true) != 0) {
            return -1;
        }
    }
    return 0;
}

bool block_available(void)
{
    return blk_present;
}
