#ifndef VOYEMU_DEV_BOOT_ID_H
#define VOYEMU_DEV_BOOT_ID_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
bool parse_boot_id(const char *text, size_t length, uint8_t output[16]);
#endif
