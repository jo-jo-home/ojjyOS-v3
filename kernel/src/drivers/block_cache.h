/*
 * ojjyOS v3 Kernel - Block Cache
 *
 * Simple LRU cache for disk blocks.
 */

#ifndef _OJJY_BLOCK_CACHE_H
#define _OJJY_BLOCK_CACHE_H

#include "../types.h"

/* Block size (matches sector size) */
#define BLOCK_SIZE 512

/* Initialize block cache */
void block_cache_init(void);

/* Read a block (uses cache if available) */
int block_cache_read(uint64_t block_num, void *buffer);

/* Write a block (write-through to disk) */
int block_cache_write(uint64_t block_num, const void *buffer);

/* Invalidate a cached block */
void block_cache_invalidate(uint64_t block_num);

/* Flush all dirty blocks to disk */
void block_cache_flush(void);

/* Print cache statistics */
void block_cache_print_stats(void);

#endif /* _OJJY_BLOCK_CACHE_H */
