/*
 * ojjyOS v3 Kernel - System Power Management
 *
 * Arch-neutral power request plumbing. The UI, console and terminal
 * request power actions here; the main loop performs the actual
 * shutdown/restart sequence (save state, notify, hand off to the
 * architecture-specific power code).
 *
 * ojjyOS - Created by Jonas Lee
 */

#ifndef _OJJY_SYSTEM_H
#define _OJJY_SYSTEM_H

#include "types.h"

typedef enum {
    POWER_REQ_NONE = 0,
    POWER_REQ_SHUTDOWN,
    POWER_REQ_RESTART,
} PowerRequest;

/*
 * Request a shutdown or restart (asynchronous: the main loop picks
 * this up and performs the save + power sequence).
 */
void system_request_shutdown(void);
void system_request_restart(void);

/*
 * Returns the pending power request (POWER_REQ_NONE if none).
 * The main loop must clear it once handled.
 */
PowerRequest system_get_request(void);
void system_clear_request(void);

/*
 * Synchronous sync request: the main loop saves persistent state
 * at the next opportunity.
 */
void system_request_sync(void);
bool system_sync_pending(void);

#endif /* _OJJY_SYSTEM_H */
