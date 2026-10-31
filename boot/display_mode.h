/* ojjyOS - Created by Jonas Lee */
#ifndef OJJY_BOOT_DISPLAY_MODE_H
#define OJJY_BOOT_DISPLAY_MODE_H

static int boot_display_size(const unsigned char *data, unsigned size,
                             unsigned *width, unsigned *height)
{
    unsigned values[2] = {0,0}, digits[2] = {0,0}, part = 0;
    if (!data || !size || size > 20 || !width || !height) return 0;
    if (!data[size - 1]) --size;
    for (unsigned index = 0; index < size; ++index) {
        if (data[index] == 'x' && part == 0 && digits[0]) { part = 1; continue; }
        if (data[index] < '0' || data[index] > '9' || ++digits[part] > 4) return 0;
        values[part] = values[part] * 10 + data[index] - '0';
    }
    if (!part || !digits[1] || values[0] < 640 || values[0] > 4096 ||
        values[1] < 480 || values[1] > 2160) return 0;
    *width = values[0]; *height = values[1];
    return 1;
}

static unsigned boot_display_be32(const unsigned char *data)
{
    return ((unsigned)data[0] << 24) | ((unsigned)data[1] << 16) |
           ((unsigned)data[2] << 8) | data[3];
}

static int boot_display_config(void (*select_item)(unsigned short), unsigned char (*read_byte)(void),
                               unsigned *width, unsigned *height)
{
    const char name[] = "opt/ojjyOS/display";
    unsigned char buffer[64];
    select_item(0);
    for (unsigned index = 0; index < 4; ++index) buffer[index] = read_byte();
    if (buffer[0] != 'Q' || buffer[1] != 'E' || buffer[2] != 'M' || buffer[3] != 'U') return 0;
    select_item(0x19);
    for (unsigned index = 0; index < 4; ++index) buffer[index] = read_byte();
    unsigned count = boot_display_be32(buffer);
    if (count > 256) return 0;
    for (unsigned entry = 0; entry < count; ++entry) {
        for (unsigned index = 0; index < 64; ++index) buffer[index] = read_byte();
        unsigned index = 0;
        while (index < sizeof(name) && buffer[8 + index] == (unsigned char)name[index]) ++index;
        if (index != sizeof(name)) continue;
        unsigned size = boot_display_be32(buffer);
        unsigned selector = ((unsigned)buffer[4] << 8) | buffer[5];
        if (!size || size > 20 || selector < 0x20 || selector >= 0x4000) return 0;
        select_item((unsigned short)selector);
        for (index = 0; index < size; ++index) buffer[index] = read_byte();
        return boot_display_size(buffer, size, width, height);
    }
    return 0;
}

#endif
