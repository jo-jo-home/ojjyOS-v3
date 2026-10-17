/* Minimal, bounded ACPI S5 literal package decoding. Created by Jonas Lee. */
#include "acpi_aml.h"
#include "../string.h"

static bool integer(const uint8_t *aml, size_t end, size_t *position, uint8_t *value)
{
    if (*position >= end) return false;
    uint8_t op = aml[(*position)++];
    if (op == 0 || op == 1) { *value = op; return true; } /* ZeroOp/OneOp */
    unsigned width = op == 0x0A ? 1 : op == 0x0B ? 2 : op == 0x0C ? 4 : op == 0x0E ? 8 : 0;
    if (!width || end - *position < width) return false;
    uint64_t number = 0;
    for (unsigned i = 0; i < width; i++) number |= (uint64_t)aml[(*position)++] << (8 * i);
    if (number > 7) return false; /* PM1 SLP_TYP is three bits. */
    *value = (uint8_t)number;
    return true;
}

bool acpi_aml_s5(const uint8_t *aml, size_t length, uint8_t types[2])
{
    if (!aml || !types) return false;
    for (size_t i = 0; i < length; i++) {
        if (aml[i] != 0x08) continue; /* NameOp */
        size_t name = i + 1;
        if (name < length && aml[name] == '\\') name++;
        if (length - name < 5 || memcmp(aml + name, "_S5_", 4) != 0 || aml[name + 4] != 0x12) continue;
        size_t start = name + 5; /* PkgLength starts after PackageOp. */
        if (start >= length) continue;
        uint8_t lead = aml[start];
        unsigned encoded_bytes = (lead >> 6) + 1;
        if (length - start < encoded_bytes) continue;
        uint32_t package_length = encoded_bytes == 1 ? lead & 0x3F : lead & 0x0F;
        for (unsigned j = 1; j < encoded_bytes; j++) {
            package_length |= (uint32_t)aml[start + j] << (4 + (j - 1) * 8);
        }
        if (package_length > length - start || package_length <= encoded_bytes) continue;
        size_t end = start + package_length;
        size_t position = start + encoded_bytes;
        if (aml[position++] < 2) continue;
        uint8_t decoded[2];
        if (integer(aml, end, &position, &decoded[0]) && integer(aml, end, &position, &decoded[1])) {
            types[0] = decoded[0]; types[1] = decoded[1];
            return true;
        }
    }
    return false;
}
