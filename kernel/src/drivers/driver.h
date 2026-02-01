/*
 * ojjyOS v3 Kernel - Driver Model
 *
 * Defines the common driver interface, registration system,
 * and error containment mechanisms.
 *
 * Architecture:
 *   - Drivers register with the driver subsystem
 *   - Each driver has probe/init/shutdown lifecycle
 *   - IRQ routing dispatches to registered handlers
 *   - Error containment prevents driver crashes from killing OS
 */

#ifndef _OJJY_DRIVER_H
#define _OJJY_DRIVER_H

#include "../types.h"

/*
 * Driver types - categorizes driver functionality
 */
typedef enum {
    DRIVER_TYPE_UNKNOWN = 0,
    DRIVER_TYPE_INPUT,      /* Keyboard, mouse, touch, etc. */
    DRIVER_TYPE_TIMER,      /* Timer/clock sources */
    DRIVER_TYPE_BLOCK,      /* Block devices (disk, flash) */
    DRIVER_TYPE_CHAR,       /* Character devices (serial, RTC) */
    DRIVER_TYPE_DISPLAY,    /* Display/GPU drivers */
    DRIVER_TYPE_NETWORK,    /* Network interfaces */
    DRIVER_TYPE_BUS,        /* Bus controllers (PCI, USB) */
} DriverType;

/*
 * Driver state machine
 */
typedef enum {
    DRIVER_STATE_UNLOADED = 0,      /* Not yet registered */
    DRIVER_STATE_REGISTERED,        /* Registered, not probed */
    DRIVER_STATE_PROBING,           /* Currently probing hardware */
    DRIVER_STATE_INITIALIZING,      /* Probe succeeded, initializing */
    DRIVER_STATE_READY,             /* Fully operational */
    DRIVER_STATE_SUSPENDED,         /* Power-suspended */
    DRIVER_STATE_ERROR,             /* In error state */
    DRIVER_STATE_DISABLED,          /* Disabled due to errors */
} DriverState;

/* Forward declaration */
struct Driver;

/*
 * Driver operations - all callbacks are optional (NULL = not supported)
 */
typedef struct {
    /*
     * probe - Check if hardware is present
     * Return true if hardware found, false otherwise
     */
    bool (*probe)(struct Driver *drv);

    /*
     * init - Initialize the driver after successful probe
     * Return 0 on success, negative error code on failure
     */
    int (*init)(struct Driver *drv);

    /*
     * shutdown - Clean shutdown of driver
     * Return 0 on success
     */
    int (*shutdown)(struct Driver *drv);

    /*
     * suspend/resume - Power management (future use)
     */
    int (*suspend)(struct Driver *drv);
    int (*resume)(struct Driver *drv);

    /*
     * handle_irq - Interrupt handler
     * Return true if IRQ was handled by this driver
     */
    bool (*handle_irq)(struct Driver *drv, uint8_t irq);

    /*
     * read - Read data from device
     * Returns bytes read, or negative error code
     */
    ssize_t (*read)(struct Driver *drv, void *buf, size_t count, uint64_t offset);

    /*
     * write - Write data to device
     * Returns bytes written, or negative error code
     */
    ssize_t (*write)(struct Driver *drv, const void *buf, size_t count, uint64_t offset);

    /*
     * ioctl - Device-specific control operations
     * Returns 0 on success, negative error code on failure
     */
    int (*ioctl)(struct Driver *drv, uint32_t cmd, void *arg);

    /*
     * poll - Check if device has data available (for async I/O)
     * Returns bitmask of ready conditions
     */
    int (*poll)(struct Driver *drv);

} DriverOps;

/*
 * Driver flags
 */
#define DRIVER_FLAG_CRITICAL    (1 << 0)    /* Cannot be disabled (kbd, timer) */
#define DRIVER_FLAG_HOTPLUG     (1 << 1)    /* Supports hot plug/unplug */
#define DRIVER_FLAG_DMA         (1 << 2)    /* Uses DMA */
#define DRIVER_FLAG_EXCLUSIVE   (1 << 3)    /* Exclusive hardware access */

/*
 * Error thresholds
 */
#define DRIVER_ERROR_THRESHOLD  10          /* Disable after this many errors */
#define DRIVER_IRQ_TIMEOUT_MS   100         /* IRQ handler timeout */

/*
 * Driver structure - represents a single driver instance
 */
typedef struct Driver {
    /* Identification */
    const char      *name;              /* Human-readable name */
    const char      *description;       /* Longer description */
    uint32_t         version;           /* Driver version (major.minor.patch) */

    /* Classification */
    DriverType       type;              /* Driver type */
    uint32_t         flags;             /* Driver flags */
    DriverState      state;             /* Current state */

    /* Operations */
    DriverOps       *ops;               /* Operation callbacks */
    void            *private_data;      /* Driver-specific data */

    /* IRQ handling */
    uint8_t          irq;               /* Primary IRQ (0xFF = none) */
    uint8_t          irq_count;         /* Number of IRQs registered */

    /* Statistics */
    uint64_t         irq_total;         /* Total IRQs handled */
    uint64_t         read_bytes;        /* Total bytes read */
    uint64_t         write_bytes;       /* Total bytes written */
    uint64_t         error_count;       /* Error count */
    uint64_t         last_error_tick;   /* Tick of last error */

    /* Linked list */
    struct Driver   *next;
} Driver;

/*
 * Convenience macro for driver version
 */
#define DRIVER_VERSION(major, minor, patch) \
    (((major) << 16) | ((minor) << 8) | (patch))

#define DRIVER_VERSION_MAJOR(v) (((v) >> 16) & 0xFF)
#define DRIVER_VERSION_MINOR(v) (((v) >> 8) & 0xFF)
#define DRIVER_VERSION_PATCH(v) ((v) & 0xFF)

/*
 * Maximum number of drivers
 */
#define MAX_DRIVERS         32
#define MAX_IRQ_HANDLERS    16      /* Max handlers per IRQ */

/*
 * Driver subsystem initialization
 */
void driver_subsystem_init(void);

/*
 * Driver registration
 */
int driver_register(Driver *drv);
int driver_unregister(Driver *drv);

/*
 * Driver lookup
 */
Driver *driver_find_by_name(const char *name);
Driver *driver_find_by_type(DriverType type);
Driver *driver_find_ready_by_type(DriverType type);

/*
 * Driver enumeration
 */
typedef void (*DriverCallback)(Driver *drv, void *context);
void driver_for_each(DriverCallback callback, void *context);
int driver_get_count(void);

/*
 * Driver lifecycle
 */
void driver_probe_all(void);
int driver_start(Driver *drv);
int driver_stop(Driver *drv);

/*
 * IRQ management
 */
void driver_register_irq(Driver *drv, uint8_t irq);
void driver_unregister_irq(Driver *drv, uint8_t irq);
bool driver_dispatch_irq(uint8_t irq);

/*
 * Error handling
 */
void driver_report_error(Driver *drv, const char *message);
void driver_clear_errors(Driver *drv);
bool driver_is_healthy(Driver *drv);

/*
 * Diagnostics
 */
void driver_print_all(void);
void driver_print_stats(Driver *drv);
const char *driver_state_string(DriverState state);
const char *driver_type_string(DriverType type);

#endif /* _OJJY_DRIVER_H */
