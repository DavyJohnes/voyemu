/* NOLINTBEGIN(modernize-use-nullptr): C source without nullptr, also included by C++ tests. */
/* Research adapter at the original 280-byte dummy MCU receive boundary. */
#include "native_mcu_async.h"
#include "native_mcu_layout.inc"
#include "original_symbol.h"
static u32 native_mcu_values[579], native_mcu_wheel[10];
static u64 native_mcu_ack, native_wheel_ack;
static unsigned native_mcu_have;
static unsigned char native_mcu_settings[10];
static unsigned native_mcu_settings_have;
static void *native_parser_library, *native_parser_entry, *native_serializer_entry;

/* Original SoC serializer exports ten settings bytes at bits240..319. The virtual
 * MCU returns successful writes at bits2000..2079; the original parser validates
 * the XOR-with-0xc5 parity before initializing its settings service. */
int dataparser_makesoc2mcudata(void *state, void *output, unsigned capacity) {
    int (*native)(void *, void *, unsigned) = (int (*)(void *, void *, unsigned))original_symbol(
            &native_parser_library, &native_serializer_entry, "/lib64/libdataparser.so.0",
            "dataparser_makesoc2mcudata");

    if (!native || native == dataparser_makesoc2mcudata) {
        return -1;
    }

    int result = native(state, output, capacity);

    if (result || !state || !output || capacity < 40 || !native_mcu_enabled()) {
        return result;
    }

    const unsigned char *settings = (const unsigned char *)output + 30;
    unsigned parity = 0xc5;

    for (unsigned i = 0; i < 9; i++) {
        parity ^= settings[i];
    }

    if (settings[9] == parity) {
        if (native_mcu_async_enabled()) {
            native_cache_wait();
            copy(native_mcu_settings, settings, 10);
            native_mcu_settings_have = 1;
            native_cache_release();
        } else {
            acquire();
            copy(native_mcu_settings, settings, 10);
            native_mcu_settings_have = 1;
            release();
        }
    }

    return result;
}

static void native_mcu_pack(unsigned char *output) {
    for (unsigned i = 0; i < 579; i++) {
        u32 value = i >= 321 && i <= 330 ? native_mcu_wheel[i - 321] : native_mcu_values[i];

        for (unsigned b = 0; b < native_mcu_width[i]; b++) {
            unsigned bit = native_mcu_bit[i] + b, mask = 1U << (bit & 7);
            output[bit >> 3] =
                    (unsigned char)((output[bit >> 3] & ~mask) | (((value >> b) & 1U) ? mask : 0));
        }
    }
}

/* A host revision is staged only when every field fits the catalogue. */
static int native_async_values_valid(const u32 *values) {
    if (values[0] != 1 || values[1] != 579) {
        return 0;
    }

    for (unsigned i = 0; i < 579; i++) {
        if (values[i + 2] > mcu_field_max[i] || (mcu_transient[i] && values[i + 2])) {
            return 0;
        }
    }

    return values[312] <= 255 && values[360] == values[312] * 10 && values[314] <= 100;
}

static void native_async_poll(void) {
    u32 values[581];
    u64 args[1];
    u64 seq = native_async_fetch(364, args, 1, &native_mcu_ack, &native_async_mcu_staged, 0, values,
                                 sizeof(values));

    if (seq && native_async_values_valid(values) &&
        !__atomic_load_n(&native_async_stop, __ATOMIC_ACQUIRE) && native_cache_try()) {
        if (native_async_mcu_staged == native_mcu_ack) {
            unsigned changed = 0;

            for (unsigned i = 0; i < 10; i++) {
                if (values[507 + i] != (native_mcu_have ? native_mcu_values[505 + i] : 0)) {
                    changed = 1;
                }
            }

            if (changed) {
                for (unsigned i = 0; i < 10; i++) {
                    native_mcu_settings[i] = (unsigned char)values[507 + i];
                }

                native_mcu_settings_have = 1;
            }

            copy(native_mcu_values, values + 2, sizeof(native_mcu_values));
            native_mcu_have = 1;
            native_async_mcu_staged = seq;
        }

        native_cache_release();
    }

    u32 wheel[2];
    seq = native_async_fetch(363, args, 1, &native_wheel_ack, &native_async_wheel_staged,
                             &native_mcu_ack, wheel, sizeof(wheel));

    if (seq && wheel[0] >= 322 && wheel[0] <= 331 && wheel[1] <= 1 &&
        !__atomic_load_n(&native_async_stop, __ATOMIC_ACQUIRE) && native_cache_try()) {
        if (native_async_wheel_staged == native_wheel_ack) {
            native_mcu_wheel[wheel[0] - 322] = wheel[1];
            native_async_wheel_staged = seq;
        }

        native_cache_release();
    }
}

/* A frame packed without the staged values carries the zeros of the original dummy frame:
 * ignition off and settings with a parity error. Wait for the bounded cache owner instead. */
static void native_async_apply(void *output) {
    native_cache_wait();

    if (native_mcu_have) {
        native_mcu_pack((unsigned char *)output);
        native_mcu_ack = native_async_mcu_staged;
        native_wheel_ack = native_async_wheel_staged;
    }

    if (native_mcu_settings_have) {
        copy((unsigned char *)output + 250, native_mcu_settings, 10);
    }

    native_cache_release();
}

int dataParser_makedumydata(void *output, unsigned short capacity) {
    int (*native)(void *, unsigned short) = (int (*)(void *, unsigned short))original_symbol(
            &native_parser_library, &native_parser_entry, "/lib64/libdataparser.so.0",
            "dataParser_makedumydata");

    if (!native || native == dataParser_makedumydata) {
        return -1;
    }

    int result = native(output, capacity);

    if (result != 280 || !output || capacity < 280 || !native_mcu_enabled()) {
        return result;
    }

    if (native_mcu_async_enabled()) {
        native_async_apply(output);

        return result;
    }

    acquire();
    u32 values[581];
    u64 args[] = {native_mcu_ack};
    u64 seq = call(364, args, 1, 0, 0);
    unsigned valid = seq && seq > native_mcu_ack && seq < 0x100000000ULL && packet &&
                     !packet->fault && packet->out_len == sizeof(values);

    if (valid) {
        copy(values, packet->data, sizeof(values));
        valid = values[0] == 1 && values[1] == 579;

        for (unsigned i = 0; valid && i < 579; i++) {
            if (values[i + 2] > mcu_field_max[i] || (mcu_transient[i] && values[i + 2])) {
                valid = 0;
            }
        }

        if (valid && (values[312] > 255 || values[360] != values[312] * 10 || values[314] > 100)) {
            valid = 0;
        }

        if (valid) {
            /* Ordinary vehicle revisions retain OEM feedback. Deliberate edits of
             * raw set_byte0..9 still override it, including malformed test data. */
            unsigned changed = 0;

            for (unsigned i = 0; i < 10; i++) {
                if (values[507 + i] != (native_mcu_have ? native_mcu_values[505 + i] : 0)) {
                    changed = 1;
                }
            }

            if (changed) {
                for (unsigned i = 0; i < 10; i++) {
                    native_mcu_settings[i] = (unsigned char)values[507 + i];
                }

                native_mcu_settings_have = 1;
            }

            copy(native_mcu_values, values + 2, sizeof(native_mcu_values));
            native_mcu_have = 1;
        }
    }

    u32 wheel[2];
    u64 wa[] = {native_wheel_ack};
    u64 wseq = call(363, wa, 1, 0, 0);
    unsigned wvalid = native_mcu_have && wseq && wseq > native_wheel_ack && wseq < 0x100000000ULL &&
                      packet && !packet->fault && packet->out_len == sizeof(wheel);

    if (wvalid) {
        copy(wheel, packet->data, sizeof(wheel));
        wvalid = wheel[0] >= 322 && wheel[0] <= 331 && wheel[1] <= 1;

        if (wvalid) {
            native_mcu_wheel[wheel[0] - 322] = wheel[1];
        }
    }

    if (native_mcu_have) {
        native_mcu_pack((unsigned char *)output);

        if (valid) {
            native_mcu_ack = seq;
        }

        if (wvalid) {
            native_wheel_ack = wseq;
        }
    }

    if (native_mcu_settings_have) {
        copy((unsigned char *)output + 250, native_mcu_settings, 10);
    }

    release();

    return result;
}

/* NOLINTEND(modernize-use-nullptr) */
