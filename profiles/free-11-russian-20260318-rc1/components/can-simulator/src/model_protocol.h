#ifndef VOYEMU_DEV_MODEL_PROTOCOL_H
#define VOYEMU_DEV_MODEL_PROTOCOL_H
#include "model_queue.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define MODEL_REQUEST_SIZE 32
#define MODEL_REPLY_SIZE 48
bool model_protocol(struct model_queue *model, bool source_present, const uint8_t *request, size_t length, uint8_t reply[MODEL_REPLY_SIZE]);
#endif
