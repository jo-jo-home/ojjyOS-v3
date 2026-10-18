/* Bounded protected ELF execution. Created by Jonas Lee. */
#ifndef OJJY_USER_H
#define OJJY_USER_H
#include "types.h"
#include "user_abi.h"

#define USER_BASE          0x100000000ULL
#define USER_IMAGE_LOW     (USER_BASE + 0x10000)
#define USER_IMAGE_HIGH    (USER_BASE + 0x100000)
#define USER_STACK_TOP     (USER_BASE + 0x1FF000)
#define USER_STACK_PAGES   4
#define USER_MAX_PAGES     64
#define USER_MAX_SEGMENTS  8
#define USER_MAX_FILE      (128 * 1024)
#define USER_OUTPUT_SIZE   4096
#define USER_WRITE_MAX     512
#define USER_SYSCALL_MAX   256
#define USER_TIME_MS       100
#define USER_R 4U
#define USER_W 2U
#define USER_X 1U

typedef struct {
    uint64_t address, offset, file_size, memory_size;
    uint32_t flags;
} UserSegment;
typedef struct {
    uint64_t entry;
    unsigned count, pages;
    UserSegment segments[USER_MAX_SEGMENTS];
} UserImage;
typedef struct { uint64_t address, physical; uint32_t flags; } UserPage;
typedef struct { unsigned count; UserPage pages[USER_MAX_PAGES]; } UserSpace;
typedef enum { USER_NOT_RUN, USER_RUNNING, USER_EXITED, USER_FAULT, USER_TIMEOUT, USER_LIMIT } UserStatus;
typedef struct {
    UserStatus status;
    int exit_code;
    uint32_t pid, uid, syscalls;
    uint64_t elapsed_ms, pc, fault_address, syndrome;
    size_t output_length;
    char output[USER_OUTPUT_SIZE];
} UserResult;

/* Pure validation / memory policy, also exercised by host tests. */
bool user_image_validate(const void *file, size_t size, uint16_t machine, UserImage *out);
bool user_space_load(UserSpace *space, const void *file, size_t size, const UserImage *image);
void user_space_destroy(UserSpace *space);
bool user_range_valid(const UserSpace *space, uint64_t address, size_t size, uint32_t flags);
bool user_copy_from(const UserSpace *space, void *dest, uint64_t source, size_t size);

/* Kernel-facing runner. -1 unsupported, -2 denied/busy, -3 bad path/image,
 * -4 resources/platform error; 0 means execution completed (inspect status). */
bool user_supported(void);
int user_run(const char *name, UserResult *result);
const char *user_status_name(UserStatus status);
const char *user_error_name(int rc);

/* Architecture trap interface, not accessible as user calls. */
bool user_is_running(void);
void user_fault(uint64_t pc, uint64_t address, uint64_t syndrome);
void user_timer_tick(void);
int64_t user_syscall(uint64_t number, uint64_t a0, uint64_t a1, uint64_t a2);

/* Architecture implementation. Intel deliberately returns unsupported. */
void user_arch_init(void);
bool user_arch_supported(void);
bool user_arch_prepare(const UserSpace *space);
void user_arch_run(uint64_t entry, uint64_t stack);
uint64_t user_arch_millis(void);
#endif
