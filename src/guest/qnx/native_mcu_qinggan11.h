#ifndef VOYEMU_NATIVE_MCU_QINGGAN11_H
#define VOYEMU_NATIVE_MCU_QINGGAN11_H
/* Only proven named Free A11 7.3.1 signals are writable. Unmapped bits retain each OEM packet. */
/* A nonzero unknown_wire replaces the normalized unknown value unknown_input on the wire. */
typedef struct {
    unsigned short bit;
    unsigned char width;
    unsigned initial, unknown_input, unknown_wire;
} Qinggan11McuField;

static int qinggan11_mcu_valid(const Qinggan11McuField *fields, const unsigned *values) {
    for (unsigned i = 0; i < 579; i++) {
        if (!fields[i].width) {
            if (values[i] != fields[i].initial) {
                return 0;
            }
        } else if (fields[i].width > 32 || fields[i].bit + fields[i].width > 650 * 8 ||
                   (fields[i].width < 32 && values[i] > ((1U << fields[i].width) - 1))) {
            return 0;
        }
    }

    return 1;
}

static int qinggan11_mcu_project(unsigned char *output, unsigned capacity, int original_result,
                                 const Qinggan11McuField *fields, const unsigned *values) {
    if (!output || capacity < 650 || original_result != 650 ||
        !qinggan11_mcu_valid(fields, values)) {
        return 0;
    }

    for (unsigned i = 0; i < 579; i++) {
        for (unsigned b = 0; b < fields[i].width; b++) {
            unsigned bit = fields[i].bit + b, mask = 1U << (bit & 7);
            output[bit >> 3] = (unsigned char)((output[bit >> 3] & ~mask) |
                                               (((values[i] >> b) & 1U) ? mask : 0));
        }
    }

    return 1;
}

static int qinggan11_mcu_wheel_supported(const Qinggan11McuField *fields, unsigned index,
                                         unsigned value) {
    return index >= 322 && index <= 331 && value <= 1 && fields[index - 1].width == 1;
}

/* Exact Free A11 7.3.1 OEM ABI: SoC bits240..319 -> MCU parserset562..571 bits2000..2079. */
typedef struct {
    unsigned char bytes[10];
    unsigned have;
} Qinggan11SettingsFeedback;

static int qinggan11_settings_capture(Qinggan11SettingsFeedback *state, int original_result,
                                      const void *output, unsigned capacity) {
    if (!state || original_result != 0 || !output || capacity < 40) {
        return 0;
    }

    const unsigned char *bytes = (const unsigned char *)output + 30;
    unsigned parity = 0xc5;

    for (unsigned i = 0; i < 9; i++) {
        parity ^= bytes[i];
    }

    if (bytes[9] != parity) {
        return 0;
    }

    for (unsigned i = 0; i < 10; i++) {
        state->bytes[i] = bytes[i];
    }

    state->have = 1;

    return 1;
}

static int qinggan11_settings_apply(const Qinggan11SettingsFeedback *state, void *output,
                                    unsigned capacity, int original_result) {
    if (!state || !state->have || !output || capacity < 650 || original_result != 650) {
        return 0;
    }

    for (unsigned i = 0; i < 10; i++) {
        ((unsigned char *)output)[250 + i] = state->bytes[i];
    }

    return 1;
}

#endif
