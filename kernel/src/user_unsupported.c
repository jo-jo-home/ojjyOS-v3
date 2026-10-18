/* Intel protected execution is a later milestone, not a simulated success. */
#include "user.h"
#include "timer.h"
void user_arch_init(void) {}
bool user_arch_supported(void) { return false; }
bool user_arch_prepare(const UserSpace *space) { (void)space; return false; }
void user_arch_run(uint64_t entry, uint64_t stack) { (void)entry; (void)stack; }
uint64_t user_arch_millis(void) { return timer_get_ticks(); }
