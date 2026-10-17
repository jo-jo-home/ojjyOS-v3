#include "drivers/acpi_aml.h"
#include "test_support.h"

int main(void)
{
    uint8_t types[2] = {9, 9};
    /* Actual QEMU 11.1.2 DSDT bytes: _S5 uses ZeroOp, not BytePrefix. */
    const uint8_t qemu[] = {0x08, '_', 'S', '5', '_', 0x12, 0x06, 4, 0, 0, 0, 0};
    CHECK(acpi_aml_s5(qemu, sizeof(qemu), types) && types[0] == 0 && types[1] == 0,
          "QEMU ZeroOp sleep types decode");
    const uint8_t bytes[] = {0x08, '\\', '_', 'S', '5', '_', 0x12, 0x06, 2, 0x0A, 5, 0x0A, 6};
    CHECK(acpi_aml_s5(bytes, sizeof(bytes), types) && types[0] == 5 && types[1] == 6,
          "root prefix and BytePrefix sleep types");
    const uint8_t word[] = {0x08, '_', 'S', '5', '_', 0x12, 0x08, 2, 0x0B, 7, 0, 0x0B, 2, 0};
    CHECK(acpi_aml_s5(word, sizeof(word), types) && types[0] == 7 && types[1] == 2,
          "WordPrefix sleep types");
    const uint8_t multi[] = {0x08, '_', 'S', '5', '_', 0x12, 0x45, 0, 2, 0, 1};
    CHECK(acpi_aml_s5(multi, sizeof(multi), types) && types[0] == 0 && types[1] == 1,
          "multi-byte PkgLength and OneOp");
    for (size_t length = 0; length < sizeof(qemu); length++) {
        CHECK(!acpi_aml_s5(qemu, length, types), "truncated AML rejected");
    }
    uint8_t invalid[sizeof(qemu)];
    memcpy(invalid, qemu, sizeof(qemu)); invalid[6] = 63;
    CHECK(!acpi_aml_s5(invalid, sizeof(invalid), types), "package outside table rejected");
    memcpy(invalid, qemu, sizeof(qemu)); invalid[7] = 1;
    CHECK(!acpi_aml_s5(invalid, sizeof(invalid), types), "S5 package needs both sleep types");
    memcpy(invalid, qemu, sizeof(qemu)); invalid[8] = 0xFF;
    CHECK(!acpi_aml_s5(invalid, sizeof(invalid), types), "unsupported integer opcode rejected");
    uint8_t bad_value[sizeof(bytes)]; memcpy(bad_value, bytes, sizeof(bytes)); bad_value[10] = 8;
    CHECK(!acpi_aml_s5(bad_value, sizeof(bad_value), types), "SLP_TYP must fit three bits");
    CHECK(!acpi_aml_s5(NULL, 100, types) && !acpi_aml_s5(qemu, sizeof(qemu), NULL), "null arguments rejected");
    return finish();
}
