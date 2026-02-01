/*
 * ojjyOS v3 Kernel - Driver Model Implementation
 *
 * Manages driver registration, lifecycle, IRQ dispatch, and error containment.
 */

#include "driver.h"
#include "../serial.h"
#include "../string.h"
#include "../console.h"
#include "../timer.h"

/*
 * Driver registry - linked list of all registered drivers
 */
static Driver *driver_list_head = NULL;
static int driver_count = 0;

/*
 * IRQ handler table - maps IRQ numbers to driver lists
 * Multiple drivers can share an IRQ (will be called in order)
 */
typedef struct {
    Driver *handlers[MAX_IRQ_HANDLERS];
    int count;
} IrqHandlerList;

static IrqHandlerList irq_table[256];

/*
 * Subsystem state
 */
static bool subsystem_initialized = false;

/*
 * State string conversion
 */
const char *driver_state_string(DriverState state)
{
    switch (state) {
        case DRIVER_STATE_UNLOADED:     return "UNLOADED";
        case DRIVER_STATE_REGISTERED:   return "REGISTERED";
        case DRIVER_STATE_PROBING:      return "PROBING";
        case DRIVER_STATE_INITIALIZING: return "INIT";
        case DRIVER_STATE_READY:        return "READY";
        case DRIVER_STATE_SUSPENDED:    return "SUSPENDED";
        case DRIVER_STATE_ERROR:        return "ERROR";
        case DRIVER_STATE_DISABLED:     return "DISABLED";
        default:                        return "UNKNOWN";
    }
}

/*
 * Type string conversion
 */
const char *driver_type_string(DriverType type)
{
    switch (type) {
        case DRIVER_TYPE_INPUT:   return "Input";
        case DRIVER_TYPE_TIMER:   return "Timer";
        case DRIVER_TYPE_BLOCK:   return "Block";
        case DRIVER_TYPE_CHAR:    return "Char";
        case DRIVER_TYPE_DISPLAY: return "Display";
        case DRIVER_TYPE_NETWORK: return "Network";
        case DRIVER_TYPE_BUS:     return "Bus";
        default:                  return "Unknown";
    }
}

/*
 * Initialize driver subsystem
 */
void driver_subsystem_init(void)
{
    serial_printf("[DRIVER] Initializing driver subsystem...\n");

    driver_list_head = NULL;
    driver_count = 0;

    /* Clear IRQ table */
    for (int i = 0; i < 256; i++) {
        irq_table[i].count = 0;
        for (int j = 0; j < MAX_IRQ_HANDLERS; j++) {
            irq_table[i].handlers[j] = NULL;
        }
    }

    subsystem_initialized = true;
    serial_printf("[DRIVER] Subsystem initialized\n");
}

/*
 * Register a driver
 */
int driver_register(Driver *drv)
{
    if (!subsystem_initialized) {
        serial_printf("[DRIVER] ERROR: Subsystem not initialized\n");
        return -1;
    }

    if (!drv || !drv->name) {
        serial_printf("[DRIVER] ERROR: Invalid driver or missing name\n");
        return -1;
    }

    if (driver_count >= MAX_DRIVERS) {
        serial_printf("[DRIVER] ERROR: Maximum drivers reached (%d)\n", MAX_DRIVERS);
        return -1;
    }

    /* Check for duplicate */
    if (driver_find_by_name(drv->name)) {
        serial_printf("[DRIVER] ERROR: Driver '%s' already registered\n", drv->name);
        return -1;
    }

    /* Initialize driver fields */
    drv->state = DRIVER_STATE_REGISTERED;
    drv->irq = 0xFF;
    drv->irq_count = 0;
    drv->irq_total = 0;
    drv->read_bytes = 0;
    drv->write_bytes = 0;
    drv->error_count = 0;
    drv->last_error_tick = 0;

    /* Add to linked list (at head for simplicity) */
    drv->next = driver_list_head;
    driver_list_head = drv;
    driver_count++;

    serial_printf("[DRIVER] Registered: %s (type=%s, version=%d.%d.%d)\n",
        drv->name,
        driver_type_string(drv->type),
        DRIVER_VERSION_MAJOR(drv->version),
        DRIVER_VERSION_MINOR(drv->version),
        DRIVER_VERSION_PATCH(drv->version));

    return 0;
}

/*
 * Unregister a driver
 */
int driver_unregister(Driver *drv)
{
    if (!drv) return -1;

    /* Cannot unregister critical drivers */
    if (drv->flags & DRIVER_FLAG_CRITICAL) {
        serial_printf("[DRIVER] ERROR: Cannot unregister critical driver '%s'\n", drv->name);
        return -1;
    }

    /* Stop the driver first */
    driver_stop(drv);

    /* Unregister all IRQs */
    for (int irq = 0; irq < 256; irq++) {
        driver_unregister_irq(drv, irq);
    }

    /* Remove from linked list */
    Driver **pp = &driver_list_head;
    while (*pp) {
        if (*pp == drv) {
            *pp = drv->next;
            driver_count--;
            drv->state = DRIVER_STATE_UNLOADED;
            drv->next = NULL;
            serial_printf("[DRIVER] Unregistered: %s\n", drv->name);
            return 0;
        }
        pp = &(*pp)->next;
    }

    return -1;  /* Not found */
}

/*
 * Find driver by name
 */
Driver *driver_find_by_name(const char *name)
{
    if (!name) return NULL;

    Driver *drv = driver_list_head;
    while (drv) {
        if (strcmp(drv->name, name) == 0) {
            return drv;
        }
        drv = drv->next;
    }
    return NULL;
}

/*
 * Find first driver of given type (any state)
 */
Driver *driver_find_by_type(DriverType type)
{
    Driver *drv = driver_list_head;
    while (drv) {
        if (drv->type == type) {
            return drv;
        }
        drv = drv->next;
    }
    return NULL;
}

/*
 * Find first READY driver of given type
 */
Driver *driver_find_ready_by_type(DriverType type)
{
    Driver *drv = driver_list_head;
    while (drv) {
        if (drv->type == type && drv->state == DRIVER_STATE_READY) {
            return drv;
        }
        drv = drv->next;
    }
    return NULL;
}

/*
 * Iterate over all drivers
 */
void driver_for_each(DriverCallback callback, void *context)
{
    if (!callback) return;

    Driver *drv = driver_list_head;
    while (drv) {
        callback(drv, context);
        drv = drv->next;
    }
}

/*
 * Get driver count
 */
int driver_get_count(void)
{
    return driver_count;
}

/*
 * Start a single driver (probe + init)
 */
int driver_start(Driver *drv)
{
    if (!drv) return -1;

    if (drv->state == DRIVER_STATE_READY) {
        return 0;  /* Already running */
    }

    if (drv->state == DRIVER_STATE_DISABLED) {
        serial_printf("[DRIVER] Cannot start disabled driver '%s'\n", drv->name);
        return -1;
    }

    serial_printf("[DRIVER] Starting '%s'...\n", drv->name);

    /* Probe phase */
    drv->state = DRIVER_STATE_PROBING;
    bool found = true;

    if (drv->ops && drv->ops->probe) {
        found = drv->ops->probe(drv);
    }

    if (!found) {
        serial_printf("[DRIVER] '%s': Hardware not found\n", drv->name);
        drv->state = DRIVER_STATE_REGISTERED;
        return -1;
    }

    /* Init phase */
    drv->state = DRIVER_STATE_INITIALIZING;
    int ret = 0;

    if (drv->ops && drv->ops->init) {
        ret = drv->ops->init(drv);
    }

    if (ret != 0) {
        serial_printf("[DRIVER] '%s': Init failed with error %d\n", drv->name, ret);
        drv->state = DRIVER_STATE_ERROR;
        driver_report_error(drv, "Init failed");
        return ret;
    }

    drv->state = DRIVER_STATE_READY;
    serial_printf("[DRIVER] '%s': Started successfully\n", drv->name);
    return 0;
}

/*
 * Stop a driver
 */
int driver_stop(Driver *drv)
{
    if (!drv) return -1;

    if (drv->state != DRIVER_STATE_READY &&
        drv->state != DRIVER_STATE_ERROR) {
        return 0;  /* Not running */
    }

    serial_printf("[DRIVER] Stopping '%s'...\n", drv->name);

    if (drv->ops && drv->ops->shutdown) {
        drv->ops->shutdown(drv);
    }

    drv->state = DRIVER_STATE_REGISTERED;
    return 0;
}

/*
 * Probe and start all registered drivers
 */
void driver_probe_all(void)
{
    serial_printf("[DRIVER] Probing all drivers...\n");

    int started = 0;
    int failed = 0;

    Driver *drv = driver_list_head;
    while (drv) {
        if (drv->state == DRIVER_STATE_REGISTERED) {
            int ret = driver_start(drv);
            if (ret == 0) {
                started++;
            } else {
                failed++;
            }
        }
        drv = drv->next;
    }

    serial_printf("[DRIVER] Probe complete: %d started, %d failed, %d total\n",
        started, failed, driver_count);
}

/*
 * Register IRQ handler for a driver
 */
void driver_register_irq(Driver *drv, uint8_t irq)
{
    if (!drv) return;

    IrqHandlerList *list = &irq_table[irq];

    /* Check if already registered */
    for (int i = 0; i < list->count; i++) {
        if (list->handlers[i] == drv) {
            return;  /* Already registered */
        }
    }

    if (list->count >= MAX_IRQ_HANDLERS) {
        serial_printf("[DRIVER] ERROR: Too many handlers for IRQ %d\n", irq);
        return;
    }

    list->handlers[list->count++] = drv;
    drv->irq_count++;

    if (drv->irq == 0xFF) {
        drv->irq = irq;  /* Set primary IRQ */
    }

    serial_printf("[DRIVER] '%s' registered for IRQ %d\n", drv->name, irq);
}

/*
 * Unregister IRQ handler
 */
void driver_unregister_irq(Driver *drv, uint8_t irq)
{
    if (!drv) return;

    IrqHandlerList *list = &irq_table[irq];

    for (int i = 0; i < list->count; i++) {
        if (list->handlers[i] == drv) {
            /* Remove by shifting remaining handlers */
            for (int j = i; j < list->count - 1; j++) {
                list->handlers[j] = list->handlers[j + 1];
            }
            list->handlers[--list->count] = NULL;
            drv->irq_count--;

            if (drv->irq == irq) {
                drv->irq = 0xFF;
            }

            serial_printf("[DRIVER] '%s' unregistered from IRQ %d\n", drv->name, irq);
            return;
        }
    }
}

/*
 * Dispatch IRQ to registered handlers
 * Returns true if any handler processed the IRQ
 */
bool driver_dispatch_irq(uint8_t irq)
{
    IrqHandlerList *list = &irq_table[irq];
    bool handled = false;

    for (int i = 0; i < list->count; i++) {
        Driver *drv = list->handlers[i];

        if (!drv || drv->state != DRIVER_STATE_READY) {
            continue;
        }

        if (drv->ops && drv->ops->handle_irq) {
            bool result = drv->ops->handle_irq(drv, irq);
            if (result) {
                drv->irq_total++;
                handled = true;
                break;  /* First handler that claims it wins */
            }
        }
    }

    return handled;
}

/*
 * Report a driver error
 */
void driver_report_error(Driver *drv, const char *message)
{
    if (!drv) return;

    drv->error_count++;
    drv->last_error_tick = timer_get_ticks();

    serial_printf("[DRIVER] ERROR in '%s': %s (total errors: %d)\n",
        drv->name, message ? message : "Unknown", (int)drv->error_count);

    /* Auto-disable non-critical drivers after threshold */
    if (!(drv->flags & DRIVER_FLAG_CRITICAL) &&
        drv->error_count >= DRIVER_ERROR_THRESHOLD) {

        serial_printf("[DRIVER] '%s' exceeded error threshold, disabling\n", drv->name);
        driver_stop(drv);
        drv->state = DRIVER_STATE_DISABLED;
    }
}

/*
 * Clear error count
 */
void driver_clear_errors(Driver *drv)
{
    if (!drv) return;
    drv->error_count = 0;
    drv->last_error_tick = 0;
}

/*
 * Check if driver is healthy
 */
bool driver_is_healthy(Driver *drv)
{
    if (!drv) return false;

    return (drv->state == DRIVER_STATE_READY &&
            drv->error_count < DRIVER_ERROR_THRESHOLD);
}

/*
 * Print all drivers
 */
void driver_print_all(void)
{
    console_printf("\n=== Registered Drivers ===\n");
    console_printf("%-16s %-8s %-10s %8s %8s\n",
        "Name", "Type", "State", "IRQs", "Errors");
    console_printf("------------------------------------------------------------\n");

    Driver *drv = driver_list_head;
    while (drv) {
        console_printf("%-16s %-8s %-10s %8llu %8llu\n",
            drv->name,
            driver_type_string(drv->type),
            driver_state_string(drv->state),
            drv->irq_total,
            drv->error_count);
        drv = drv->next;
    }

    console_printf("------------------------------------------------------------\n");
    console_printf("Total: %d driver(s)\n", driver_count);
}

/*
 * Print detailed stats for a driver
 */
void driver_print_stats(Driver *drv)
{
    if (!drv) return;

    console_printf("\nDriver: %s\n", drv->name);
    if (drv->description) {
        console_printf("  Description: %s\n", drv->description);
    }
    console_printf("  Version: %d.%d.%d\n",
        DRIVER_VERSION_MAJOR(drv->version),
        DRIVER_VERSION_MINOR(drv->version),
        DRIVER_VERSION_PATCH(drv->version));
    console_printf("  Type: %s\n", driver_type_string(drv->type));
    console_printf("  State: %s\n", driver_state_string(drv->state));
    console_printf("  Flags: 0x%x", drv->flags);
    if (drv->flags & DRIVER_FLAG_CRITICAL) console_printf(" [CRITICAL]");
    console_printf("\n");
    console_printf("  Primary IRQ: %d\n", drv->irq != 0xFF ? drv->irq : -1);
    console_printf("  Stats:\n");
    console_printf("    IRQs handled: %llu\n", drv->irq_total);
    console_printf("    Bytes read: %llu\n", drv->read_bytes);
    console_printf("    Bytes written: %llu\n", drv->write_bytes);
    console_printf("    Errors: %llu\n", drv->error_count);
}
