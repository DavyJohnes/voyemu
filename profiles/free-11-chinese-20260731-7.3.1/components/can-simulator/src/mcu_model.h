#ifndef VOYEMU_DEV_MCU_MODEL_H
#define VOYEMU_DEV_MCU_MODEL_H
#include <stdbool.h>
#include <stdint.h>
/* Development model; fields remain unknown until supplied by an input or command. */
struct mcu_model {
    bool forced_known, warning_known;
    uint8_t forced, warning, eps, retention, propulsion, daily_repeat;
};
/* An accepted command mutates the model but is not itself feedback delivery. */
bool mcu_apply(struct mcu_model *model, const uint8_t frame[10]);
bool mcu_forced_feedback(const struct mcu_model *model, uint8_t frame[10]);
bool mcu_warning_feedback(const struct mcu_model *model, uint8_t frame[10]);
#endif
