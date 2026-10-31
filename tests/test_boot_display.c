/* ojjyOS - Created by Jonas Lee */
#include "test_support.h"
#include "../boot/display_mode.h"

static unsigned char directory[132], value[20];
static unsigned position, selected;
static void select_item(unsigned short selector) { selected = selector; position = 0; }
static unsigned char read_byte(void)
{
    if (selected == 0) return (unsigned char)"QEMU"[position++ % 4];
    if (selected == 0x19) return directory[position++ % sizeof(directory)];
    return value[position++ % sizeof(value)];
}

int main(void)
{
    unsigned width = 9, height = 9;
    const char *good[] = {"1440x900", "1280x800", "1600x900", "3200x1800"};
    for (unsigned index = 0; index < 4; ++index)
        CHECK(boot_display_size((const unsigned char *)good[index], (unsigned)strlen(good[index]), &width, &height), "supported size parsed");
    const char *bad[] = {"", "x900", "1440x", "1440xx900", "640x0", "9999x9999", "12x12", "1440x900;echo", "99999999999999x900"};
    for (unsigned index = 0; index < 9; ++index)
        CHECK(!boot_display_size((const unsigned char *)bad[index], (unsigned)strlen(bad[index]), &width, &height), "invalid size refused");
    CHECK(!boot_display_size(NULL, 8, &width, &height), "null input refused");
    CHECK(!boot_display_size((const unsigned char *)good[0], 8, NULL, &height), "null output refused");
    directory[3] = 2;
    directory[4 + 64 + 3] = 8;
    directory[4 + 64 + 5] = 0x25;
    memcpy(directory + 4 + 64 + 8, "opt/ojjyOS/display", sizeof("opt/ojjyOS/display"));
    memcpy(value, "1280x800", 8);
    CHECK(boot_display_config(select_item, read_byte, &width, &height) && width == 1280 && height == 800,
          "big-endian directory scan selects exact named file");
    directory[4 + 64 + 3] = 21;
    CHECK(!boot_display_config(select_item, read_byte, &width, &height), "oversized config refused");
    directory[4 + 64 + 3] = 8; directory[4 + 64 + 5] = 0x10;
    CHECK(!boot_display_config(select_item, read_byte, &width, &height), "reserved selector refused");
    directory[0] = 1;
    CHECK(!boot_display_config(select_item, read_byte, &width, &height), "unbounded directory refused");
    return finish();
}
