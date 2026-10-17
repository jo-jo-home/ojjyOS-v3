/*
 * ojjyOS v3 Kernel - Block Device Abstraction (x86_64 implementation)
 *
 * Routes sector I/O through the ATA/IDE primary disk.
 *
 * ojjyOS - Created by Jonas Lee
 */

#include "blockdev.h"
#include "ata.h"

int block_read_sectors(uint64_t lba, void *buf, uint32_t count)
{
    AtaDevice *dev = ata_get_primary_disk();
    if (!dev || !buf || count == 0) return -1;
    return ata_read_sectors(dev, lba, count, buf);
}

int block_write_sectors(uint64_t lba, const void *buf, uint32_t count)
{
    AtaDevice *dev = ata_get_primary_disk();
    if (!dev || !buf || count == 0) return -1;
    return ata_write_sectors(dev, lba, (uint32_t)count, buf);
}

bool block_available(void)
{
    return ata_get_primary_disk() != NULL;
}
