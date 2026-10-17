/*
 * ojjyOS v3 Kernel - Process Table & Syscall Subsystem
 * Created by Jonas Lee
 *
 * Manages kernel & user-session task descriptors (PID, PPID, UID, owner,
 * CPU ticks, memory footprint, state) and the kernel syscall dispatch table.
 */

#ifndef _OJJY_PROC_H
#define _OJJY_PROC_H

#include "types.h"

#define PROC_MAX_PROCESSES  24
#define PROC_NAME_MAX       32

typedef enum {
    PROC_UNUSED   = 0,
    PROC_RUNNING  = 1,
    PROC_SLEEPING = 2,
    PROC_BLOCKED  = 3,
    PROC_ZOMBIE   = 4
} ProcState;

typedef struct {
    bool used;
    uint32_t pid;
    uint32_t ppid;
    uint32_t uid;
    int app_type;           /* -1 for kernel/system daemons, >=0 for CompositorAppType */
    bool is_system;         /* true for protected core daemons (PID 1..4) */
    ProcState state;
    char name[PROC_NAME_MAX];
    char owner[PROC_NAME_MAX];
    uint64_t start_tick;
    uint64_t cpu_ticks;
    uint64_t memory_bytes;
    uint32_t syscalls_invoked;
} ProcEntry;

/* Kernel System Call Numbers */
typedef enum {
    SYS_GETPID = 1,
    SYS_GETUID = 2,
    SYS_UPTIME = 3,
    SYS_OPEN   = 4,
    SYS_READ   = 5,
    SYS_WRITE  = 6,
    SYS_CLOSE  = 7,
    SYS_MKDIR  = 8,
    SYS_UNLINK = 9,
    SYS_MALLOC = 10,
    SYS_FREE   = 11,
    SYS_KILL   = 12,
    SYS_SYNC   = 13
} SyscallNumber;

/* Initialize process table and spawn core system daemons (PID 1..4) */
void proc_init(void);

/* Spawn a new process entry; returns PID (>0) or -1 on error */
int proc_spawn(const char *name, uint32_t ppid, uint32_t uid,
               const char *owner, int app_type, uint64_t mem_bytes, bool is_system);

/* Ensure an application process exists for app_type (owned by active user) */
int proc_ensure_app(int app_type, const char *name, uint64_t mem_bytes);

/* Terminate the process entry associated with app_type when its last window closes */
int proc_terminate_app(int app_type);

/* Kill process by PID: returns 0 on success, -1 not found, -2 protected system daemon.
 * If killed_app_type is non-NULL, stores the killed process's app_type (-1 if none). */
int proc_kill(uint32_t pid, int *killed_app_type);

/* Update CPU tick accounting and process states */
void proc_tick(uint64_t delta_ticks, int focused_app_type);

/* Query process table */
int proc_count(void);
const ProcEntry *proc_get_by_index(int index);
const ProcEntry *proc_get_by_pid(uint32_t pid);
const ProcEntry *proc_get_by_app(int app_type);
const char *proc_state_name(ProcState state);

/* Dispatch a kernel system call */
int64_t syscall_dispatch(uint32_t num, uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3);
uint64_t proc_get_total_syscalls(void);

#endif /* _OJJY_PROC_H */
