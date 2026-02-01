/*
 * ojjyOS v3 Kernel - String Utilities Implementation
 */

#include "string.h"

/*
 * Set memory to a value
 */
void *memset(void *s, int c, size_t n)
{
    uint8_t *p = (uint8_t *)s;
    while (n--) {
        *p++ = (uint8_t)c;
    }
    return s;
}

/*
 * Copy memory (non-overlapping)
 */
void *memcpy(void *dest, const void *src, size_t n)
{
    uint8_t *d = (uint8_t *)dest;
    const uint8_t *s = (const uint8_t *)src;
    while (n--) {
        *d++ = *s++;
    }
    return dest;
}

/*
 * Copy memory (overlapping safe)
 */
void *memmove(void *dest, const void *src, size_t n)
{
    uint8_t *d = (uint8_t *)dest;
    const uint8_t *s = (const uint8_t *)src;

    if (d < s) {
        while (n--) {
            *d++ = *s++;
        }
    } else {
        d += n;
        s += n;
        while (n--) {
            *--d = *--s;
        }
    }
    return dest;
}

/*
 * Compare memory
 */
int memcmp(const void *s1, const void *s2, size_t n)
{
    const uint8_t *p1 = (const uint8_t *)s1;
    const uint8_t *p2 = (const uint8_t *)s2;

    while (n--) {
        if (*p1 != *p2) {
            return *p1 - *p2;
        }
        p1++;
        p2++;
    }
    return 0;
}

/*
 * String length
 */
size_t strlen(const char *s)
{
    size_t len = 0;
    while (*s++) {
        len++;
    }
    return len;
}

/*
 * Copy string
 */
char *strcpy(char *dest, const char *src)
{
    char *d = dest;
    while ((*d++ = *src++));
    return dest;
}

/*
 * Copy string with limit
 */
char *strncpy(char *dest, const char *src, size_t n)
{
    char *d = dest;
    while (n && (*d++ = *src++)) {
        n--;
    }
    while (n--) {
        *d++ = '\0';
    }
    return dest;
}

/*
 * Compare strings
 */
int strcmp(const char *s1, const char *s2)
{
    while (*s1 && (*s1 == *s2)) {
        s1++;
        s2++;
    }
    return *(unsigned char *)s1 - *(unsigned char *)s2;
}

/*
 * Compare strings with limit
 */
int strncmp(const char *s1, const char *s2, size_t n)
{
    while (n && *s1 && (*s1 == *s2)) {
        s1++;
        s2++;
        n--;
    }
    if (n == 0) return 0;
    return *(unsigned char *)s1 - *(unsigned char *)s2;
}

/*
 * Convert signed integer to string
 */
void itoa(int64_t value, char *str, int base)
{
    char *p = str;
    char *p1, *p2;
    uint64_t uvalue;
    char tmp;

    /* Handle negative numbers for base 10 */
    if (value < 0 && base == 10) {
        *p++ = '-';
        uvalue = (uint64_t)(-(value + 1)) + 1;
    } else {
        uvalue = (uint64_t)value;
    }

    /* Generate digits in reverse */
    p1 = p;
    do {
        int digit = uvalue % base;
        *p++ = (digit < 10) ? ('0' + digit) : ('a' + digit - 10);
        uvalue /= base;
    } while (uvalue);

    /* Terminate string */
    *p = '\0';

    /* Reverse the digits */
    p2 = p - 1;
    while (p1 < p2) {
        tmp = *p1;
        *p1++ = *p2;
        *p2-- = tmp;
    }
}

/*
 * Convert unsigned integer to string
 */
void utoa(uint64_t value, char *str, int base)
{
    char *p = str;
    char *p1, *p2;
    char tmp;

    /* Generate digits in reverse */
    p1 = p;
    do {
        int digit = value % base;
        *p++ = (digit < 10) ? ('0' + digit) : ('a' + digit - 10);
        value /= base;
    } while (value);

    /* Terminate string */
    *p = '\0';

    /* Reverse the digits */
    p2 = p - 1;
    while (p1 < p2) {
        tmp = *p1;
        *p1++ = *p2;
        *p2-- = tmp;
    }
}
