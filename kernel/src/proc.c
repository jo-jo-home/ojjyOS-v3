/*
 * ojjyOS v3 Kernel - Process Table & Syscall Subsystem
 * Created by Jonas Lee
 */

#include "proc.h"
#include "users.h"
#include "memory.h"
#include "timer.h"
#include "serial.h"
#include "string.h"
#include "system.h"
#include "fs/vfs.h"

static ProcEntry proc_table[PROC_MAX_PROCESSES];
static uint32_t next_pid = 1;
static uint32_t current_pid = 1;
static uint64_t total_syscalls = 0;
static bool proc_ready = false;

const char *proc_state_name(ProcState state)
{
    switch (state) {
    case PROC_RUNNING:  return "Running";
    case PROC_SLEEPING: return "Sleeping";
    case PROC_BLOCKED:  return "Blocked";
    case PROC_ZOMBIE:   return "Zombie";
    default:            return "Unused";
    }
}

void proc_init(void)
{
    memset(proc_table, 0, sizeof(proc_table));
    next_pid = 1;
    current_pid = 1;
    total_syscalls = 0;
    proc_ready = true;

    /* Spawn core system daemons */
    proc_spawn("kernel_task",  0, 0, "root", -1, 2048ULL * 1024ULL, true);
    proc_spawn("vfs_persistd", 1, 0, "root", -1, 1024ULL * 1024ULL, true);
    proc_spawn("inputd",       1, 0, "root", -1,   64ULL * 1024ULL, true);
    proc_spawn("WindowServer", 1, 0, "root", -1, 4096ULL * 1024ULL, true);

    /* vfs_persistd sleeps between 30-second checkpoints */
    if (proc_table[1].used) {
        proc_table[1].state = PROC_SLEEPING;
    }

    serial_printf("[PROC] Process table initialized (4 system tasks active)\n");
}

int proc_spawn(const char *name, uint32_t ppid, uint32_t uid,
               const char *owner, int app_type, uint64_t mem_bytes, bool is_system)
{
    if (!proc_ready) proc_init();
    if (!name || !name[0]) return -1;

    int slot = -1;
    for (int i = 0; i < PROC_MAX_PROCESSES; i++) {
        if (!proc_table[i].used) {
            slot = i;
            break;
        }
    }
    if (slot < 0) return -1;

    ProcEntry *p = &proc_table[slot];
    memset(p, 0, sizeof(*p));
    p->used = true;
    p->pid = next_pid++;
    p->ppid = ppid;
    p->uid = uid;
    p->app_type = app_type;
    p->is_system = is_system;
    p->state = PROC_RUNNING;
    strncpy(p->name, name, PROC_NAME_MAX - 1);
    p->name[PROC_NAME_MAX - 1] = '\0';
    strncpy(p->owner, (owner && owner[0]) ? owner : "root", PROC_NAME_MAX - 1);
    p->owner[PROC_NAME_MAX - 1] = '\0';
    p->start_tick = timer_get_ticks();
    p->cpu_ticks = 1;
    p->memory_bytes = mem_bytes;
    p->syscalls_invoked = 0;

    return (int)p->pid;
}

int proc_ensure_app(int app_type, const char *name, uint64_t mem_bytes)
{
    if (!proc_ready) proc_init();
    if (app_type < 0) return -1;

    int uidx = users_current_index();
    uint32_t uid = (uidx >= 0) ? (uint32_t)(501 + uidx) : 500U;
    const char *owner = users_current_username();

    for (int i = 0; i < PROC_MAX_PROCESSES; i++) {
        if (proc_table[i].used && proc_table[i].app_type == app_type) {
            proc_table[i].state = PROC_RUNNING;
            proc_table[i].uid = uid;
            strncpy(proc_table[i].owner, owner, PROC_NAME_MAX - 1);
            proc_table[i].owner[PROC_NAME_MAX - 1] = '\0';
            if (mem_bytes > 0) proc_table[i].memory_bytes = mem_bytes;
            return (int)proc_table[i].pid;
        }
    }

    return proc_spawn(name ? name : "App", 4 /* WindowServer */, uid, owner,
                      app_type, mem_bytes > 0 ? mem_bytes : (256ULL * 1024ULL), false);
}

int proc_terminate_app(int app_type)
{
    if (!proc_ready || app_type < 0) return -1;
    for (int i = 0; i < PROC_MAX_PROCESSES; i++) {
        if (proc_table[i].used && proc_table[i].app_type == app_type) {
            proc_table[i].used = false;
            return 0;
        }
    }
    return -1;
}

int proc_kill(uint32_t pid, int *killed_app_type)
{
    if (killed_app_type) *killed_app_type = -1;
    if (!proc_ready) proc_init();

    for (int i = 0; i < PROC_MAX_PROCESSES; i++) {
        if (proc_table[i].used && proc_table[i].pid == pid) {
            if (proc_table[i].is_system) {
                return -2; /* Cannot kill core system task */
            }
            if (killed_app_type) {
                *killed_app_type = proc_table[i].app_type;
            }
            serial_printf("[PROC] Terminated PID %d (%s)\n",
                          (int)proc_table[i].pid, proc_table[i].name);
            proc_table[i].used = false;
            return 0;
        }
    }
    return -1;
}

void proc_tick(uint64_t delta_ticks, int focused_app_type)
{
    if (!proc_ready) proc_init();
    if (delta_ticks == 0) delta_ticks = 1;

    int uidx = users_current_index();
    uint32_t active_uid = (uidx >= 0) ? (uint32_t)(501 + uidx) : 500U;

    for (int i = 0; i < PROC_MAX_PROCESSES; i++) {
        if (!proc_table[i].used) continue;
        ProcEntry *p = &proc_table[i];
        if (p->pid == 1) {
            /* kernel_task */
            p->cpu_ticks += delta_ticks;
            p->state = PROC_RUNNING;
        } else if (p->pid == 2) {
            /* vfs_persistd */
            p->state = PROC_SLEEPING;
        } else if (p->pid == 3) {
            /* inputd */
            p->cpu_ticks += (delta_ticks > 1 ? delta_ticks / 2 : 1);
            p->state = PROC_RUNNING;
        } else if (p->pid == 4) {
            /* WindowServer */
            p->cpu_ticks += delta_ticks;
            p->state = PROC_RUNNING;
            p->uid = active_uid;
            strncpy(p->owner, users_current_username(), PROC_NAME_MAX - 1);
            p->owner[PROC_NAME_MAX - 1] = '\0';
        } else if (p->app_type >= 0) {
            if (p->app_type == focused_app_type) {
                p->state = PROC_RUNNING;
                p->cpu_ticks += delta_ticks;
                current_pid = p->pid;
            } else {
                p->state = PROC_SLEEPING;
            }
        }
    }
}

int proc_count(void)
{
    if (!proc_ready) proc_init();
    int count = 0;
    for (int i = 0; i < PROC_MAX_PROCESSES; i++) {
        if (proc_table[i].used) count++;
    }
    return count;
}

const ProcEntry *proc_get_by_index(int index)
{
    if (!proc_ready) proc_init();
    if (index < 0) return NULL;
    int seen = 0;
    for (int i = 0; i < PROC_MAX_PROCESSES; i++) {
        if (proc_table[i].used) {
            if (seen == index) return &proc_table[i];
            seen++;
        }
    }
    return NULL;
}

const ProcEntry *proc_get_by_pid(uint32_t pid)
{
    if (!proc_ready) proc_init();
    for (int i = 0; i < PROC_MAX_PROCESSES; i++) {
        if (proc_table[i].used && proc_table[i].pid == pid) {
            return &proc_table[i];
        }
    }
    return NULL;
}

const ProcEntry *proc_get_by_app(int app_type)
{
    if (!proc_ready || app_type < 0) return NULL;
    for (int i = 0; i < PROC_MAX_PROCESSES; i++) {
        if (proc_table[i].used && proc_table[i].app_type == app_type) {
            return &proc_table[i];
        }
    }
    return NULL;
}

int64_t syscall_dispatch(uint32_t num, uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3)
{
    (void)a3;
    if (!proc_ready) proc_init();
    total_syscalls++;

    for (int i = 0; i < PROC_MAX_PROCESSES; i++) {
        if (proc_table[i].used && proc_table[i].pid == current_pid) {
            proc_table[i].syscalls_invoked++;
            break;
        }
    }

    switch ((SyscallNumber)num) {
    case SYS_GETPID:
        return (int64_t)current_pid;
    case SYS_GETUID: {
        int uidx = users_current_index();
        return (int64_t)((uidx >= 0) ? (501 + uidx) : 500);
    }
    case SYS_UPTIME:
        return (int64_t)timer_get_ticks();
    case SYS_OPEN:
        return (int64_t)(uintptr_t)vfs_open((const char *)(uintptr_t)a0, (uint32_t)a1);
    case SYS_READ:
        return (int64_t)vfs_read((VfsFile *)(uintptr_t)a0, (void *)(uintptr_t)a1, (size_t)a2);
    case SYS_WRITE:
        return (int64_t)vfs_write((VfsFile *)(uintptr_t)a0, (const void *)(uintptr_t)a1, (size_t)a2);
    case SYS_CLOSE:
        vfs_close((VfsFile *)(uintptr_t)a0);
        return 0;
    case SYS_MKDIR:
        return (int64_t)vfs_mkdir((const char *)(uintptr_t)a0);
    case SYS_UNLINK:
        return (int64_t)vfs_unlink((const char *)(uintptr_t)a0);
    case SYS_MALLOC:
        return (int64_t)(uintptr_t)kmalloc((size_t)a0);
    case SYS_FREE:
        kfree((void *)(uintptr_t)a0);
        return 0;
    case SYS_KILL:
        return (int64_t)proc_kill((uint32_t)a0, NULL);
    case SYS_SYNC:
        system_request_sync();
        return 0;
    default:
        return -1;
    }
}

uint64_t proc_get_total_syscalls(void)
{
    return total_syscalls;
}
