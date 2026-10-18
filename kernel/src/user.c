/* Bounded EL0 runner and checked syscall policy. Created by Jonas Lee. */
#include "user.h"
#include "proc.h"
#include "users.h"
#include "memory.h"
#include "string.h"
#include "serial.h"
#include "fs/vfs.h"

static uint8_t file_buffer[USER_MAX_FILE] __attribute__((aligned(16)));
static struct {
    bool busy;
    uint64_t started;
    UserSpace space;
    UserResult result;
} task;

const char *user_status_name(UserStatus status)
{
    switch (status) {
    case USER_RUNNING: return "running";
    case USER_EXITED: return "exited";
    case USER_FAULT: return "fault";
    case USER_TIMEOUT: return "timeout";
    case USER_LIMIT: return "limit";
    default: return "not-run";
    }
}

bool user_supported(void) { return user_arch_supported(); }
const char *user_error_name(int rc)
{
    switch (rc) {
    case -1: return "protected execution unsupported on this architecture";
    case -2: return "login required or runner busy";
    case -3: return "invalid name or ELF (try: run hello)";
    default: return "insufficient resources or platform unavailable";
    }
}
bool user_is_running(void) { return task.busy && task.result.status == USER_RUNNING; }

void user_fault(uint64_t pc, uint64_t address, uint64_t syndrome)
{
    if (!user_is_running()) return;
    task.result.status = USER_FAULT;
    task.result.exit_code = -11;
    task.result.pc = pc; task.result.fault_address = address; task.result.syndrome = syndrome;
}

void user_timer_tick(void)
{
    if (user_is_running() && user_arch_millis() - task.started >= USER_TIME_MS) {
        task.result.status = USER_TIMEOUT;
        task.result.exit_code = -110;
    }
}

int64_t user_syscall(uint64_t number, uint64_t a0, uint64_t a1, uint64_t a2)
{
    if (!user_is_running()) return -1;
    user_timer_tick();
    if (!user_is_running()) return -110;
    if (++task.result.syscalls > USER_SYSCALL_MAX) {
        task.result.status = USER_LIMIT; task.result.exit_code = -125;
        return -125;
    }
    switch (number) {
    case USER_SYS_GETPID: return task.result.pid;
    case USER_SYS_GETUID: return task.result.uid;
    case USER_SYS_UPTIME: return (int64_t)user_arch_millis();
    case USER_SYS_EXIT:
        if ((int64_t)a0 < -2147483648LL || (int64_t)a0 > 2147483647LL) return -22;
        task.result.status = USER_EXITED; task.result.exit_code = (int)(int64_t)a0;
        return 0;
    case USER_SYS_WRITE: {
        if (a0 != 1 && a0 != 2) return -9;
        if (a2 > USER_WRITE_MAX) return -22;
        if (a2 > USER_OUTPUT_SIZE - 1 - task.result.output_length) return -28;
        char buffer[USER_WRITE_MAX];
        if (!user_copy_from(&task.space, buffer, a1, a2)) return -14;
        for (size_t i = 0; i < a2; i++) {
            uint8_t c = (uint8_t)buffer[i];
            task.result.output[task.result.output_length++] =
                (c == '\n' || c == '\t' || (c >= 32 && c <= 126)) ? (char)c : '?';
        }
        task.result.output[task.result.output_length] = '\0';
        return (int64_t)a2;
    }
    default: return -38; /* The old raw-pointer kernel dispatcher is NEVER called. */
    }
}

int user_run(const char *name, UserResult *result)
{
    if (!result) return -2;
    memset(result, 0, sizeof(*result));
    if (!user_supported()) return -1;
    if (task.busy || !users_is_authenticated()) return -2;
    if (!name || !name[0] || strlen(name) > 32) return -3;
    for (size_t i = 0; name[i]; i++) {
        if (!((name[i] >= 'a' && name[i] <= 'z') || (name[i] >= '0' && name[i] <= '9') ||
              name[i] == '-' || name[i] == '_')) return -3;
    }
    char path[96]; strcpy(path, "/System/Programs/");
    strcpy(path + strlen(path), name); strcpy(path + strlen(path), ".elf");
    VfsStat stat;
    if (vfs_stat(path, &stat) != 0 || stat.type != VFS_TYPE_FILE || stat.size > USER_MAX_FILE) return -3;
    VfsFile *file = vfs_open(path, VFS_O_READ);
    if (!file) return -3;
    size_t done = 0;
    while (done < stat.size) {
        ssize_t bytes = vfs_read(file, file_buffer + done, stat.size - done);
        if (bytes <= 0 || (uint64_t)bytes > stat.size - done) break;
        done += (size_t)bytes;
    }
    vfs_close(file);
    UserImage image;
    if (done != stat.size || !user_image_validate(file_buffer, done, 183, &image)) return -3;
    memset(&task, 0, sizeof(task)); task.busy = true;
    uint64_t free_before = pmm_get_free_memory();
    int rc = -4;
    if (!user_space_load(&task.space, file_buffer, done, &image)) goto cleanup;
    int index = users_current_index();
    task.result.uid = (uint32_t)(index >= 0 ? 501 + index : 500);
    int pid = proc_spawn(name, 1, task.result.uid, users_current_username(), -2,
                         (uint64_t)task.space.count * PAGE_SIZE, false);
    if (pid < 0) goto cleanup;
    task.result.pid = (uint32_t)pid;
    if (!user_arch_prepare(&task.space)) goto cleanup;
    task.result.status = USER_RUNNING;
    task.started = user_arch_millis();
    serial_printf("[USER] Enter EL0 pid=%u name=%s pages=%u\n", task.result.pid, name, task.space.count);
    user_arch_run(image.entry, USER_STACK_TOP);
    task.result.elapsed_ms = user_arch_millis() - task.started;
    if (task.result.status == USER_RUNNING) user_fault(0, 0, 0);
    rc = 0;
cleanup:
    if (task.result.pid) proc_kill(task.result.pid, NULL);
    /* Backend must already be back on the kernel tables before freeing. */
    user_space_destroy(&task.space);
    *result = task.result;
    serial_printf("[USER] Reaped pid=%u status=%s code=%d syscalls=%u ms=%llu pages=%u\n",
                  result->pid, user_status_name(result->status), result->exit_code,
                  result->syscalls, result->elapsed_ms, task.space.count);
    serial_printf("[USER] Cleanup free_before=%llu free_after=%llu pc=%llx far=%llx esr=%llx\n",
                  free_before, pmm_get_free_memory(), result->pc, result->fault_address, result->syndrome);
    task.busy = false;
    return rc;
}
