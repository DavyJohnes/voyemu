#ifndef VOYEMU_DEV_OBSERVER_PROTOCOL_H
#define VOYEMU_DEV_OBSERVER_PROTOCOL_H
#include "model_oracle.h"
#include "model_queue.h"
#include <stdbool.h>
#include <stddef.h>
#define OBSERVER_REQUEST_SIZE 24
#define OBSERVER_REPLY_SIZE 148
bool observer_protocol(const struct model_oracle *oracle, const struct model_queue *model,
                       uint64_t now, const uint8_t *request, size_t length,
                       uint8_t reply[OBSERVER_REPLY_SIZE]);
#endif
