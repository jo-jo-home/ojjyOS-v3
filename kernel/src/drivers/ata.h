/*
 * ojjyOS v3 Kernel - ATA/IDE Disk Driver
 *
 * Simple PIO mode ATA driver for reading/writing disk sectors.
 * Works with VirtualBox's PIIX4 IDE controller.
 */

#ifndef _OJJY_ATA_H
#define _OJJY_ATA_H

#include "../types.h"
#include "driver.h"

/* Sector size */
#define ATA_SECTOR_SIZE 512

/* Maximum number of ATA devices */
#define ATA_MAX_DEVICES 4

/* ATA device info */
typedef struct {
    bool     present;           /* Device exists */
    bool     is_atapi;          /* ATAPI (CD-ROM) device */
    uint8_t  channel;           /* 0 = primary, 1 = secondary */
    uint8_t  drive;             /* 0 = master, 1 = slave */
    uint64_t sectors;           /* Total sectors (LBA48) */
    uint32_t sectors_28;        /* Total sectors (LBA28) */
    char     model[41];         /* Model string */
    char     serial[21];        /* Serial number */
    bool     supports_lba48;    /* Supports 48-bit LBA */
} AtaDevice;

/* Get the ATA driver */
Driver *ata_get_driver(void);

/* Initialize ATA subsystem (registers driver) */
void ata_init(void);

/* Get device info */
AtaDevice *ata_get_device(int index);

/* Get number of detected devices */
int ata_get_device_count(void);

/* Read sectors (blocking) */
int ata_read_sectors(AtaDevice *dev, uint64_t lba, uint32_t count, void *buffer);

/* Write sectors (blocking) */
int ata_write_sectors(AtaDevice *dev, uint64_t lba, uint32_t count, const void *buffer);

/* Print device info */
void ata_print_devices(void);

#endif /* _OJJY_ATA_H */
