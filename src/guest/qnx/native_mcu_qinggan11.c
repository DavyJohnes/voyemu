/* NOLINTBEGIN(modernize-use-nullptr): C source without nullptr, also included by C++ tests. */
/* Versioned Free A11 original650-byte parser boundary, not the A9 280-byte layout. */
#include "native_mcu_qinggan11.h"
#include "native_mcu_async.h"
#include "original_symbol.h"
#ifndef QGFX_QINGGAN11_LAYOUT_DEFINED
#include "native_mcu_qinggan11_layout.inc"
#endif
static u32 qinggan11_values[579], qinggan11_wheel[10];
static u64 qinggan11_ack, qinggan11_wheel_ack;
static unsigned qinggan11_have, qinggan11_rejects;
static Qinggan11SettingsFeedback qinggan11_settings;
/* Bounded evidence from original packets; never synthesize a settings notification. */
static struct {
    unsigned have, count;
    unsigned char bytes[10];
} qinggan11_settings_trace[3];

static void qinggan11_settings_observe(unsigned lane, const unsigned char *bytes) {
    unsigned parity = 0xc5;

    for (unsigned i = 0; i < 9; i++) {
        parity ^= bytes[i];
    }

    if (lane >= 3 || bytes[9] != parity || qinggan11_settings_trace[lane].count >= 8) {
        return;
    }

    unsigned changed = !qinggan11_settings_trace[lane].have;

    for (unsigned i = 0; i < 10; i++) {
        if (qinggan11_settings_trace[lane].bytes[i] != bytes[i]) {
            changed = 1;
        }
    }

    if (!changed) {
        return;
    }

    static const char hex[] = "0123456789abcdef";
    char encoded[21], line[96];

    for (unsigned i = 0; i < 10; i++) {
        qinggan11_settings_trace[lane].bytes[i] = bytes[i];
        encoded[(unsigned long long)i * 2] = hex[bytes[i] >> 4];
        encoded[i * 2 + 1] = hex[bytes[i] & 15];
    }

    encoded[20] = 0;
    qinggan11_settings_trace[lane].have = 1;
    qinggan11_settings_trace[lane].count++;
    snprintf(line, sizeof(line), "QG3 SETTINGS Free A11 7.3.1 lane=%u sample=%u bytes=%s\n", lane,
             qinggan11_settings_trace[lane].count, encoded);
    emit(line);
}

static void qinggan11_reject(void) {
    if (qinggan11_rejects++ < 4) {
        char line[128];
        snprintf(
                line, sizeof(line),
                "QG3 MCU Free A11 rejected unsupported or invalid normalized state; projection=%u/579\n",
                qinggan11_projected_fields);
        emit(line);
    }
}

static int qinggan11_payload_valid(const u32 *values) {
    if (values[0] != 1 || values[1] != 579) {
        return 0;
    }

    for (unsigned i = 0; i < 579; i++) {
        if (values[i + 2] > mcu_field_max[i] || (mcu_transient[i] && values[i + 2])) {
            return 0;
        }
    }

    return values[312] <= 255 && values[360] == values[312] * 10 && values[314] <= 100 &&
           qinggan11_mcu_valid(qinggan11_fields, values + 2);
}

/* Wheel phases override only the mapped transient fields of the current values. */
static int qinggan11_pack(void *output, unsigned capacity, int result) {
    u32 applied[579];

    copy(applied, qinggan11_values, sizeof(applied));

    for (unsigned i = 0; i < 10; i++) {
        if (qinggan11_mcu_wheel_supported(qinggan11_fields, 322 + i, 0)) {
            applied[321 + i] = qinggan11_wheel[i];
        }
    }

    for (unsigned i = 0; i < 579; i++) {
        if (qinggan11_fields[i].unknown_wire && applied[i] == qinggan11_fields[i].unknown_input) {
            applied[i] = qinggan11_fields[i].unknown_wire;
        }
    }

    if (!qinggan11_mcu_project((unsigned char *)output, capacity, result, qinggan11_fields,
                               applied)) {
        return 0;
    }

    /* Profile-owned simulated calibration fixture; OEM renderer is unchanged. */
    if (qinggan11_calibration_fixture_enabled) {
        ((unsigned char *)output)[58] = (unsigned char)((((unsigned char *)output)[58] & ~6U) |
                                                        (qinggan11_calibration_fixture << 1));
    }

    return 1;
}

static void native_async_poll(void) {
    u32 values[581];
    u64 args[] = {0, 650, 736, qinggan11_projected_fields};
    u64 seq = native_async_fetch(364, args, 4, &qinggan11_ack, &native_async_mcu_staged, 0, values,
                                 sizeof(values));

    if (seq && !qinggan11_payload_valid(values)) {
        qinggan11_reject();
        seq = 0;
    }

    if (seq && !__atomic_load_n(&native_async_stop, __ATOMIC_ACQUIRE) && native_cache_try()) {
        if (native_async_mcu_staged == qinggan11_ack) {
            copy(qinggan11_values, values + 2, sizeof(qinggan11_values));
            qinggan11_have = 1;
            native_async_mcu_staged = seq;
        }

        native_cache_release();
    }

    u32 wheel[2];
    u64 wheel_args[1];
    seq = native_async_fetch(363, wheel_args, 1, &qinggan11_wheel_ack, &native_async_wheel_staged,
                             &qinggan11_ack, wheel, sizeof(wheel));

    if (seq && qinggan11_mcu_wheel_supported(qinggan11_fields, wheel[0], wheel[1]) &&
        !__atomic_load_n(&native_async_stop, __ATOMIC_ACQUIRE) && native_cache_try()) {
        if (native_async_wheel_staged == qinggan11_wheel_ack) {
            qinggan11_wheel[wheel[0] - 322] = wheel[1];
            native_async_wheel_staged = seq;
        }

        native_cache_release();
    }
}

static void *qinggan11_parser_library, *qinggan11_parser_entry, *qinggan11_serializer_entry;

int dataparser_makesoc2mcudata(void *state, void *output, unsigned capacity) {
    int (*original)(void *, void *, unsigned) = (int (*)(void *, void *, unsigned))original_symbol(
            &qinggan11_parser_library, &qinggan11_serializer_entry, "/lib64/libdataparser.so.0",
            "dataparser_makesoc2mcudata");
    int result = original && original != dataparser_makesoc2mcudata
                         ? original(state, output, capacity)
                         : -1;

    if (native_mcu_enabled() && qinggan11_settings_feedback_enabled) {
        const int async = native_mcu_async_enabled();
        unsigned char captured[10];

        if (async) {
            native_cache_wait();
        } else {
            acquire();
        }

        const int have = qinggan11_settings_capture(&qinggan11_settings, result, output, capacity);
        copy(captured, qinggan11_settings.bytes, sizeof(captured));

        if (async) {
            native_cache_release();
        } else {
            release();
        }

        if (have) {
            qinggan11_settings_observe(0, captured);
        }
    }

    return result;
}

int dataParser_makedumydata(void *output, unsigned short capacity) {
    int (*original)(void *, unsigned short) = (int (*)(void *, unsigned short))original_symbol(
            &qinggan11_parser_library, &qinggan11_parser_entry, "/lib64/libdataparser.so.0",
            "dataParser_makedumydata");

    if (!original || original == dataParser_makedumydata) {
        return -1;
    }

    int result = original(output, capacity);

    if (result != 650 || !output || capacity < 650 || !native_mcu_enabled()) {
        return result;
    }

    if (qinggan11_settings_feedback_enabled) {
        qinggan11_settings_observe(1, (const unsigned char *)output + 250);
    }

    if (native_mcu_async_enabled()) {
        /* The original consumer never waits on host RPC; it packs the staged revision. */
        native_cache_wait();

        if (qinggan11_have && qinggan11_pack(output, capacity, result)) {
            qinggan11_ack = native_async_mcu_staged;
            qinggan11_wheel_ack = native_async_wheel_staged;
        }

        const int settings = qinggan11_settings_feedback_enabled &&
                             qinggan11_settings_apply(&qinggan11_settings, output, capacity,
                                                      result);
        native_cache_release();

        if (settings) {
            qinggan11_settings_observe(2, (const unsigned char *)output + 250);
        }

        return result;
    }

    acquire();
    u32 values[581];
    u64 args[] = {qinggan11_ack, 650, 736, qinggan11_projected_fields};
    u64 seq = call(364, args, 4, 0, 0);
    unsigned valid = seq && seq > qinggan11_ack && seq < 0x100000000ULL && packet &&
                     !packet->fault && packet->out_len == sizeof(values);

    if (valid) {
        copy(values, packet->data, sizeof(values));
        valid = qinggan11_payload_valid(values);

        if (!valid) {
            qinggan11_reject();
        }
    }

    if (valid) {
        copy(qinggan11_values, values + 2, sizeof(qinggan11_values));
        qinggan11_have = 1;
    }

    u32 wheel[2];
    u64 wa[] = {qinggan11_wheel_ack};
    u64 wseq = qinggan11_have ? call(363, wa, 1, 0, 0) : 0;
    unsigned wvalid = wseq && wseq > qinggan11_wheel_ack && wseq < 0x100000000ULL && packet &&
                      !packet->fault && packet->out_len == sizeof(wheel);

    if (wvalid) {
        copy(wheel, packet->data, sizeof(wheel));
        wvalid = qinggan11_mcu_wheel_supported(qinggan11_fields, wheel[0], wheel[1]);

        if (wvalid) {
            qinggan11_wheel[wheel[0] - 322] = wheel[1];
        }
    }

    if (qinggan11_have && qinggan11_pack(output, capacity, result)) {
        if (valid) {
            qinggan11_ack = seq;
        }

        if (wvalid) {
            qinggan11_wheel_ack = wseq;
        }
    }

    const int settings = qinggan11_settings_feedback_enabled &&
                         qinggan11_settings_apply(&qinggan11_settings, output, capacity, result);
    release();

    if (settings) {
        qinggan11_settings_observe(2, (const unsigned char *)output + 250);
    }

    return result;
}

/* NOLINTEND(modernize-use-nullptr) */
