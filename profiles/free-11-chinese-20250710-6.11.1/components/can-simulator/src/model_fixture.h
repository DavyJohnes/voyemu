#ifndef VOYEMU_DEV_MODEL_FIXTURE_H
#define VOYEMU_DEV_MODEL_FIXTURE_H
#include "mcu_model.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
bool model_fixture_parse(const char *bytes, size_t size, struct mcu_model *model, uint8_t *drive, int32_t *initial_acc);
#endif
