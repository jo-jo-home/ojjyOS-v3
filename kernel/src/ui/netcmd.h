/*
 * ojjyOS v3 - Network shell commands (Terminal / console)
 *
 * ifconfig, net on|off|renew, ping, nslookup, curl. Long-running commands
 * become a single background job polled every main-loop tick, printing
 * through a callback, so the desktop never blocks on the network.
 *
 * ojjyOS - Created by Jonas Lee
 */
#ifndef _OJJY_NETCMD_H
#define _OJJY_NETCMD_H

#include "types.h"

typedef void (*netcmd_print_fn)(const char *line);

bool netcmd_exec(const char *cmd, netcmd_print_fn print);   /* true if handled */
bool netcmd_busy(void);
void netcmd_poll(netcmd_print_fn print);
void netcmd_cancel(netcmd_print_fn print);

#endif /* _OJJY_NETCMD_H */
