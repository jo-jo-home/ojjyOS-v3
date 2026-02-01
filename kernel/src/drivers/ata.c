/*
 * ojjyOS v3 Kernel - ATA/IDE Disk Driver Implementation
 *
 * PIO mode ATA driver. Simple but functional for MVP.
 * VirtualBox uses PIIX4 which presents as standard IDE.
 *
 * Channels:
 *   Primary:   I/O 0x1F0-0x1F7, Control 0x3F6, IRQ 14
 *   Secondary: I/O 0x170-0x177, Control 0x376, IRQ 15
 */

#include "ata.h"
#include "driver.h"
#include "../serial.h"
#include "../string.h"
#include "../console.h"

/* ATA I/O ports (relative to base) */
#define ATA_REG_DATA        0x00
#define ATA_REG_ERROR       0x01
#define ATA_REG_FEATURES    0x01
#define ATA_REG_SECCOUNT    0x02
#define ATA_REG_LBA_LO      0x03
#define ATA_REG_LBA_MID     0x04
#define ATA_REG_LBA_HI      0x05
#define ATA_REG_DRIVE       0x06
#define ATA_REG_STATUS      0x07
#define ATA_REG_COMMAND     0x07

/* Control register (relative to control base) */
#define ATA_REG_CONTROL     0x00
#define ATA_REG_ALTSTATUS   0x00

/* ATA commands */
#define ATA_CMD_READ_PIO        0x20
#define ATA_CMD_READ_PIO_EXT    0x24
#define ATA_CMD_WRITE_PIO       0x30
#define ATA_CMD_WRITE_PIO_EXT   0x34
#define ATA_CMD_IDENTIFY        0xEC
#define ATA_CMD_IDENTIFY_PACKET 0xA1
#define ATA_CMD_FLUSH           0xE7

/* Status register bits */
#define ATA_SR_BSY      0x80    /* Busy */
#define ATA_SR_DRDY     0x40    /* Drive ready */
#define ATA_SR_DF       0x20    /* Drive fault */
#define ATA_SR_DSC      0x10    /* Drive seek complete */
#define ATA_SR_DRQ      0x08    /* Data request */
#define ATA_SR_CORR     0x04    /* Corrected data */
#define ATA_SR_IDX      0x02    /* Index */
#define ATA_SR_ERR      0x01    /* Error */

/* Error register bits */
#define ATA_ER_ABRT     0x04    /* Command aborted */

/* Drive select bits */
#define ATA_DRIVE_MASTER    0xA0
#define ATA_DRIVE_SLAVE     0xB0
#define ATA_DRIVE_LBA       0x40

/* Channel info */
typedef struct {
    uint16_t io_base;
    uint16_t ctrl_base;
    uint8_t  irq;
} AtaChannel;

static AtaChannel channels[2] = {
    { .io_base = 0x1F0, .ctrl_base = 0x3F6, .irq = 14 },
    { .io_base = 0x170, .ctrl_base = 0x376, .irq = 15 },
};

/* Detected devices */
static AtaDevice devices[ATA_MAX_DEVICES];
static int device_count = 0;

/* Forward declarations */
static bool ata_probe(Driver *drv);
static int ata_init_driver(Driver *drv);
static ssize_t ata_read(Driver *drv, void *buf, size_t count, uint64_t offset);
static ssize_t ata_write(Driver *drv, const void *buf, size_t count, uint64_t offset);

/* Driver operations */
static DriverOps ata_ops = {
    .probe = ata_probe,
    .init = ata_init_driver,
    .read = ata_read,
    .write = ata_write,
};

/* Driver instance */
static Driver ata_driver = {
    .name = "ata",
    .description = "ATA/IDE PIO Mode Disk Driver",
    .version = DRIVER_VERSION(1, 0, 0),
    .type = DRIVER_TYPE_BLOCK,
    .ops = &ata_ops,
};

/*
 * Wait for BSY to clear
 */
static bool ata_wait_busy(AtaChannel *ch, int timeout_ms)
{
    for (int i = 0; i < timeout_ms * 1000; i++) {
        uint8_t status = inb(ch->io_base + ATA_REG_STATUS);
        if (!(status & ATA_SR_BSY)) {
            return true;
        }
        /* Small delay */
        inb(ch->ctrl_base + ATA_REG_ALTSTATUS);
    }
    return false;
}

/*
 * Wait for DRQ or error
 */
static int ata_wait_drq(AtaChannel *ch)
{
    for (int i = 0; i < 500000; i++) {
        uint8_t status = inb(ch->io_base + ATA_REG_STATUS);

        if (status & ATA_SR_ERR) {
            return -1;  /* Error */
        }
        if (status & ATA_SR_DF) {
            return -2;  /* Device fault */
        }
        if (status & ATA_SR_DRQ) {
            return 0;   /* Ready */
        }
    }
    return -3;  /* Timeout */
}

/*
 * Select drive
 */
static void ata_select_drive(AtaChannel *ch, int drive)
{
    uint8_t sel = (drive == 0) ? ATA_DRIVE_MASTER : ATA_DRIVE_SLAVE;
    outb(ch->io_base + ATA_REG_DRIVE, sel);

    /* 400ns delay (read alternate status 4 times) */
    inb(ch->ctrl_base + ATA_REG_ALTSTATUS);
    inb(ch->ctrl_base + ATA_REG_ALTSTATUS);
    inb(ch->ctrl_base + ATA_REG_ALTSTATUS);
    inb(ch->ctrl_base + ATA_REG_ALTSTATUS);
}

/*
 * Soft reset channel
 */
static void ata_soft_reset(AtaChannel *ch)
{
    outb(ch->ctrl_base + ATA_REG_CONTROL, 0x04);  /* Set SRST */
    inb(ch->ctrl_base + ATA_REG_ALTSTATUS);
    inb(ch->ctrl_base + ATA_REG_ALTSTATUS);
    inb(ch->ctrl_base + ATA_REG_ALTSTATUS);
    inb(ch->ctrl_base + ATA_REG_ALTSTATUS);
    outb(ch->ctrl_base + ATA_REG_CONTROL, 0x00);  /* Clear SRST */
    ata_wait_busy(ch, 100);
}

/*
 * Read IDENTIFY data
 */
static bool ata_identify(AtaChannel *ch, int drive, AtaDevice *dev)
{
    uint16_t identify[256];

    /* Select drive */
    ata_select_drive(ch, drive);
    if (!ata_wait_busy(ch, 100)) {
        return false;
    }

    /* Check if drive exists by reading status */
    uint8_t status = inb(ch->io_base + ATA_REG_STATUS);
    if (status == 0x00 || status == 0xFF) {
        return false;
    }

    /* Clear sector count and LBA registers */
    outb(ch->io_base + ATA_REG_SECCOUNT, 0);
    outb(ch->io_base + ATA_REG_LBA_LO, 0);
    outb(ch->io_base + ATA_REG_LBA_MID, 0);
    outb(ch->io_base + ATA_REG_LBA_HI, 0);

    /* Send IDENTIFY command */
    outb(ch->io_base + ATA_REG_COMMAND, ATA_CMD_IDENTIFY);

    /* Check for no device */
    status = inb(ch->io_base + ATA_REG_STATUS);
    if (status == 0) {
        return false;
    }

    /* Wait for response */
    if (!ata_wait_busy(ch, 1000)) {
        return false;
    }

    /* Check for ATAPI device */
    uint8_t lba_mid = inb(ch->io_base + ATA_REG_LBA_MID);
    uint8_t lba_hi = inb(ch->io_base + ATA_REG_LBA_HI);

    if (lba_mid == 0x14 && lba_hi == 0xEB) {
        /* ATAPI device - try IDENTIFY PACKET */
        outb(ch->io_base + ATA_REG_COMMAND, ATA_CMD_IDENTIFY_PACKET);
        if (!ata_wait_busy(ch, 1000)) {
            return false;
        }
        dev->is_atapi = true;
    } else if (lba_mid == 0 && lba_hi == 0) {
        dev->is_atapi = false;
    } else {
        /* Unknown device */
        return false;
    }

    /* Wait for DRQ */
    if (ata_wait_drq(ch) != 0) {
        return false;
    }

    /* Read identify data */
    for (int i = 0; i < 256; i++) {
        identify[i] = inw(ch->io_base + ATA_REG_DATA);
    }

    /* Parse identify data */
    dev->present = true;
    dev->channel = (ch == &channels[0]) ? 0 : 1;
    dev->drive = drive;

    /* LBA28 sector count (words 60-61) */
    dev->sectors_28 = identify[60] | ((uint32_t)identify[61] << 16);

    /* LBA48 support and sector count (words 83, 100-103) */
    dev->supports_lba48 = (identify[83] & (1 << 10)) != 0;
    if (dev->supports_lba48) {
        dev->sectors = identify[100] |
                      ((uint64_t)identify[101] << 16) |
                      ((uint64_t)identify[102] << 32) |
                      ((uint64_t)identify[103] << 48);
    } else {
        dev->sectors = dev->sectors_28;
    }

    /* Model string (words 27-46) - byte swapped */
    for (int i = 0; i < 20; i++) {
        dev->model[i * 2] = identify[27 + i] >> 8;
        dev->model[i * 2 + 1] = identify[27 + i] & 0xFF;
    }
    dev->model[40] = '\0';

    /* Trim trailing spaces */
    for (int i = 39; i >= 0 && dev->model[i] == ' '; i--) {
        dev->model[i] = '\0';
    }

    /* Serial number (words 10-19) */
    for (int i = 0; i < 10; i++) {
        dev->serial[i * 2] = identify[10 + i] >> 8;
        dev->serial[i * 2 + 1] = identify[10 + i] & 0xFF;
    }
    dev->serial[20] = '\0';

    for (int i = 19; i >= 0 && dev->serial[i] == ' '; i--) {
        dev->serial[i] = '\0';
    }

    return true;
}

/*
 * Probe for ATA devices
 */
static bool ata_probe(Driver *drv)
{
    (void)drv;

    serial_printf("[ATA] Probing for ATA devices...\n");

    device_count = 0;
    memset(devices, 0, sizeof(devices));

    /* Scan both channels */
    for (int ch = 0; ch < 2; ch++) {
        AtaChannel *channel = &channels[ch];

        /* Soft reset the channel */
        ata_soft_reset(channel);

        /* Try master and slave */
        for (int drv_idx = 0; drv_idx < 2; drv_idx++) {
            int idx = ch * 2 + drv_idx;
            if (ata_identify(channel, drv_idx, &devices[idx])) {
                serial_printf("[ATA] Found %s device at %s %s: %s\n",
                    devices[idx].is_atapi ? "ATAPI" : "ATA",
                    ch == 0 ? "primary" : "secondary",
                    drv_idx == 0 ? "master" : "slave",
                    devices[idx].model);
                device_count++;
            }
        }
    }

    serial_printf("[ATA] Found %d device(s)\n", device_count);
    return device_count > 0;
}

/*
 * Initialize ATA driver
 */
static int ata_init_driver(Driver *drv)
{
    (void)drv;

    serial_printf("[ATA] Initializing...\n");

    /* Disable interrupts for PIO mode (we'll poll) */
    outb(channels[0].ctrl_base + ATA_REG_CONTROL, 0x02);
    outb(channels[1].ctrl_base + ATA_REG_CONTROL, 0x02);

    serial_printf("[ATA] Initialized in PIO mode\n");
    return 0;
}

/*
 * Read sectors from device
 */
int ata_read_sectors(AtaDevice *dev, uint64_t lba, uint32_t count, void *buffer)
{
    if (!dev || !dev->present || count == 0 || !buffer) {
        return -1;
    }

    if (dev->is_atapi) {
        serial_printf("[ATA] ATAPI read not implemented\n");
        return -1;
    }

    AtaChannel *ch = &channels[dev->channel];
    uint8_t *buf = (uint8_t *)buffer;

    serial_printf("[ATA] Reading %d sectors from LBA %d\n", count, (int)lba);

    /* Select drive with LBA mode */
    uint8_t drive_sel = (dev->drive == 0) ? ATA_DRIVE_MASTER : ATA_DRIVE_SLAVE;
    drive_sel |= ATA_DRIVE_LBA;

    while (count > 0) {
        uint32_t chunk = (count > 256) ? 256 : count;

        /* Wait for not busy */
        if (!ata_wait_busy(ch, 500)) {
            driver_report_error(&ata_driver, "Timeout waiting for drive");
            return -1;
        }

        if (dev->supports_lba48 && (lba >= 0x10000000 || chunk > 256)) {
            /* LBA48 mode */
            outb(ch->io_base + ATA_REG_DRIVE, drive_sel);

            /* High bytes first */
            outb(ch->io_base + ATA_REG_SECCOUNT, (chunk >> 8) & 0xFF);
            outb(ch->io_base + ATA_REG_LBA_LO, (lba >> 24) & 0xFF);
            outb(ch->io_base + ATA_REG_LBA_MID, (lba >> 32) & 0xFF);
            outb(ch->io_base + ATA_REG_LBA_HI, (lba >> 40) & 0xFF);

            /* Low bytes */
            outb(ch->io_base + ATA_REG_SECCOUNT, chunk & 0xFF);
            outb(ch->io_base + ATA_REG_LBA_LO, lba & 0xFF);
            outb(ch->io_base + ATA_REG_LBA_MID, (lba >> 8) & 0xFF);
            outb(ch->io_base + ATA_REG_LBA_HI, (lba >> 16) & 0xFF);

            outb(ch->io_base + ATA_REG_COMMAND, ATA_CMD_READ_PIO_EXT);
        } else {
            /* LBA28 mode */
            outb(ch->io_base + ATA_REG_DRIVE, drive_sel | ((lba >> 24) & 0x0F));
            outb(ch->io_base + ATA_REG_SECCOUNT, chunk & 0xFF);
            outb(ch->io_base + ATA_REG_LBA_LO, lba & 0xFF);
            outb(ch->io_base + ATA_REG_LBA_MID, (lba >> 8) & 0xFF);
            outb(ch->io_base + ATA_REG_LBA_HI, (lba >> 16) & 0xFF);

            outb(ch->io_base + ATA_REG_COMMAND, ATA_CMD_READ_PIO);
        }

        /* Read each sector */
        for (uint32_t i = 0; i < chunk; i++) {
            if (ata_wait_drq(ch) != 0) {
                driver_report_error(&ata_driver, "DRQ timeout during read");
                return -1;
            }

            /* Read 256 words (512 bytes) */
            for (int j = 0; j < 256; j++) {
                uint16_t word = inw(ch->io_base + ATA_REG_DATA);
                buf[j * 2] = word & 0xFF;
                buf[j * 2 + 1] = (word >> 8) & 0xFF;
            }

            buf += ATA_SECTOR_SIZE;
        }

        lba += chunk;
        count -= chunk;
    }

    return 0;
}

/*
 * Write sectors to device
 */
int ata_write_sectors(AtaDevice *dev, uint64_t lba, uint32_t count, const void *buffer)
{
    if (!dev || !dev->present || count == 0 || !buffer) {
        return -1;
    }

    if (dev->is_atapi) {
        serial_printf("[ATA] ATAPI write not supported\n");
        return -1;
    }

    AtaChannel *ch = &channels[dev->channel];
    const uint8_t *buf = (const uint8_t *)buffer;

    serial_printf("[ATA] Writing %d sectors to LBA %d\n", count, (int)lba);

    uint8_t drive_sel = (dev->drive == 0) ? ATA_DRIVE_MASTER : ATA_DRIVE_SLAVE;
    drive_sel |= ATA_DRIVE_LBA;

    while (count > 0) {
        uint32_t chunk = (count > 256) ? 256 : count;

        if (!ata_wait_busy(ch, 500)) {
            driver_report_error(&ata_driver, "Timeout waiting for drive");
            return -1;
        }

        /* LBA28 mode for simplicity */
        outb(ch->io_base + ATA_REG_DRIVE, drive_sel | ((lba >> 24) & 0x0F));
        outb(ch->io_base + ATA_REG_SECCOUNT, chunk & 0xFF);
        outb(ch->io_base + ATA_REG_LBA_LO, lba & 0xFF);
        outb(ch->io_base + ATA_REG_LBA_MID, (lba >> 8) & 0xFF);
        outb(ch->io_base + ATA_REG_LBA_HI, (lba >> 16) & 0xFF);

        outb(ch->io_base + ATA_REG_COMMAND, ATA_CMD_WRITE_PIO);

        /* Write each sector */
        for (uint32_t i = 0; i < chunk; i++) {
            if (ata_wait_drq(ch) != 0) {
                driver_report_error(&ata_driver, "DRQ timeout during write");
                return -1;
            }

            /* Write 256 words */
            for (int j = 0; j < 256; j++) {
                uint16_t word = buf[j * 2] | ((uint16_t)buf[j * 2 + 1] << 8);
                outw(ch->io_base + ATA_REG_DATA, word);
            }

            buf += ATA_SECTOR_SIZE;
        }

        /* Flush cache */
        outb(ch->io_base + ATA_REG_COMMAND, ATA_CMD_FLUSH);
        ata_wait_busy(ch, 500);

        lba += chunk;
        count -= chunk;
    }

    return 0;
}

/*
 * Driver read wrapper
 */
static ssize_t ata_read(Driver *drv, void *buf, size_t count, uint64_t offset)
{
    (void)drv;

    /* Use first available device */
    AtaDevice *dev = NULL;
    for (int i = 0; i < ATA_MAX_DEVICES; i++) {
        if (devices[i].present && !devices[i].is_atapi) {
            dev = &devices[i];
            break;
        }
    }

    if (!dev) return -1;

    uint64_t lba = offset / ATA_SECTOR_SIZE;
    uint32_t sectors = (count + ATA_SECTOR_SIZE - 1) / ATA_SECTOR_SIZE;

    if (ata_read_sectors(dev, lba, sectors, buf) != 0) {
        return -1;
    }

    return count;
}

/*
 * Driver write wrapper
 */
static ssize_t ata_write(Driver *drv, const void *buf, size_t count, uint64_t offset)
{
    (void)drv;

    AtaDevice *dev = NULL;
    for (int i = 0; i < ATA_MAX_DEVICES; i++) {
        if (devices[i].present && !devices[i].is_atapi) {
            dev = &devices[i];
            break;
        }
    }

    if (!dev) return -1;

    uint64_t lba = offset / ATA_SECTOR_SIZE;
    uint32_t sectors = (count + ATA_SECTOR_SIZE - 1) / ATA_SECTOR_SIZE;

    if (ata_write_sectors(dev, lba, sectors, buf) != 0) {
        return -1;
    }

    return count;
}

/*
 * Get device info
 */
AtaDevice *ata_get_device(int index)
{
    if (index < 0 || index >= ATA_MAX_DEVICES) {
        return NULL;
    }
    return &devices[index];
}

/*
 * Get device count
 */
int ata_get_device_count(void)
{
    return device_count;
}

/*
 * Print device info
 */
void ata_print_devices(void)
{
    console_printf("\n=== ATA Devices ===\n");

    for (int i = 0; i < ATA_MAX_DEVICES; i++) {
        AtaDevice *dev = &devices[i];
        if (!dev->present) continue;

        const char *ch_str = (dev->channel == 0) ? "Primary" : "Secondary";
        const char *drv_str = (dev->drive == 0) ? "Master" : "Slave";

        console_printf("[%d] %s %s: %s\n", i, ch_str, drv_str, dev->model);
        console_printf("    Type: %s, LBA48: %s\n",
            dev->is_atapi ? "ATAPI" : "ATA",
            dev->supports_lba48 ? "Yes" : "No");

        if (!dev->is_atapi) {
            uint64_t size_mb = (dev->sectors * ATA_SECTOR_SIZE) / (1024 * 1024);
            console_printf("    Size: %d MB (%d sectors)\n",
                (int)size_mb, (int)dev->sectors);
        }
    }

    if (device_count == 0) {
        console_printf("  No devices found\n");
    }
    console_printf("\n");
}

/*
 * Get driver
 */
Driver *ata_get_driver(void)
{
    return &ata_driver;
}

/*
 * Initialize ATA (registers driver)
 */
void ata_init(void)
{
    driver_register(&ata_driver);
}
