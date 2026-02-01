/*
 * ojjyOS v3 Kernel - String Utilities
 */

#ifndef _OJJY_STRING_H
#define _OJJY_STRING_H

#include "types.h"

/* Memory operations */
void *memset(void *s, int c, size_t n);
void *memcpy(void *dest, const void *src, size_t n);
void *memmove(void *dest, const void *src, size_t n);
int memcmp(const void *s1, const void *s2, size_t n);

/* String operations */
size_t strlen(const char *s);
char *strcpy(char *dest, const char *src);
char *strncpy(char *dest, const char *src, size_t n);
int strcmp(const char *s1, const char *s2);
int strncmp(const char *s1, const char *s2, size_t n);

/* Number to string */
void itoa(int64_t value, char *str, int base);
void utoa(uint64_t value, char *str, int base);

#endif /* _OJJY_STRING_H */
