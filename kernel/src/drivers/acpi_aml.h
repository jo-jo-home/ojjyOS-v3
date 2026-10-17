/* Bounded literal _S5 package reader, not an AML interpreter. Jonas Lee. */
#ifndef OJJY_ACPI_AML_H
#define OJJY_ACPI_AML_H
#include "../types.h"
bool acpi_aml_s5(const uint8_t *aml, size_t length, uint8_t types[2]);
#endif
