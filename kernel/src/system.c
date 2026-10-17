/*
 * ojjyOS v3 Kernel - System Power Management (implementation)
 *
 * ojjyOS - Created by Jonas Lee
 */

#include "system.h"

static volatile int power_request = (int)POWER_REQ_NONE;
static volatile bool sync_pending = false;

void system_request_shutdown(void)
{
    power_request = (int)POWER_REQ_SHUTDOWN;
}

void system_request_restart(void)
{
    power_request = (int)POWER_REQ_RESTART;
}

PowerRequest system_get_request(void)
{
    return (PowerRequest)power_request;
}

void system_clear_request(void)
{
    power_request = (int)POWER_REQ_NONE;
}

void system_request_sync(void)
{
    sync_pending = true;
}

bool system_sync_pending(void)
{
    if (sync_pending) {
        sync_pending = false;
        return true;
    }
    return false;
}
