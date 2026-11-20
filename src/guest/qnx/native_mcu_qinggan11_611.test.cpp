#include "model.hpp"
#include "native_mcu_qinggan11.h"
#include "qnx_qinggan11_mcu_layout.hpp"
#include "qnx_runtime_layout.hpp"
#include <algorithm>
#include <cassert>
#include <cstring>
#include <iostream>
using Json = voyemu::Json;
using u32 = unsigned;
using u64 = unsigned long long;
using usize = unsigned long;
static Qinggan11McuField qinggan11_fields[579];
static unsigned qinggan11_calibration_fixture = 1, qinggan11_calibration_fixture_enabled = 1;
static unsigned qinggan11_projected_fields = voyemu::qnx_qinggan11_611_projected_fields,
                qinggan11_settings_feedback_enabled = 0;
#define QGFX_QINGGAN11_LAYOUT_DEFINED 1
#include "mcu_catalog.inc"
static u32 normal[579];
static unsigned char original_bytes[650];
struct TestPacket {
    u32 fault, out_len;
    unsigned char data[3840];
};
static TestPacket storage{}, *packet = &storage;
static bool enabled = true, bad = false;
static u64 offered = 2;
static unsigned original_calls = 0, rpc_calls = 0;

static void acquire() {}

static void release() {}

static void copy(void *d, const void *s, usize n) {
    std::memcpy(d, s, n);
}

static void emit(const char *) {}

static char *test_getenv(const char *name) {
    return enabled && !std::strcmp(name, "QGFX_NATIVE_MCU") ? (char *)"1" : nullptr;
}

static int test_usleep(unsigned) {
    return 0;
}

static int test_pthread_create(int *, const void *, void *(*)(void *), void *) {
    return 11;
}

static int test_pthread_join(int, void **) {
    return 3;
}

static int genuine(void *out, unsigned short size) {
    original_calls++;

    if (out && size >= 650) {
        std::memcpy(out, original_bytes, 650);
    }

    return 650;
}

static int serializer(void *, void *, unsigned) {
    return -7;
}

static void *test_dlopen(const char *, int) {
    return (void *)1;
}

static void *test_dlsym(void *, const char *n) {
    return !std::strcmp(n, "dataParser_makedumydata") ? (void *)genuine : (void *)serializer;
}

static u64 call(u32 op, const u64 *args, unsigned count, const void *, unsigned) {
    rpc_calls++;
    storage = {};

    if (op == 363) {
        assert(count == 1);

        return 0;
    }

    assert(op == 364 && count == 4 && args[1] == 650 && args[2] == 736 &&
           args[3] == qinggan11_projected_fields);
    u32 values[581] = {1, 579};
    std::memcpy(values + 2, normal, sizeof normal);

    if (bad) {
        values[2 + 6] ^= 1;
    }

    storage.out_len = sizeof values;
    std::memcpy(storage.data, values, sizeof values);

    return offered;
}

#define QGFX_QINGGAN11_611 1
#define getenv test_getenv
#define dlopen test_dlopen
#define dlsym test_dlsym
#define usleep test_usleep
#define pthread_create test_pthread_create
#define pthread_join test_pthread_join
#define QGFX_INITIALIZER
#define QGFX_NATIVE_MCU_DESTRUCTOR
#include "native_mcu_qinggan11.c"
#undef getenv
#undef dlopen
#undef dlsym
#undef usleep
#undef pthread_create
#undef pthread_join

int main(int argc, char **argv) {
    try {
        assert(argc == 2);
        const auto root = std::filesystem::canonical(argv[1]);
        const auto source = voyemu::read_private_json(
                root / "src/guest/qnx/native_mcu_qinggan11_611.json", 256 * 1024);
        const auto generated = voyemu::qnx_qinggan11_mcu_layout(source,
                                                                Json{{"eps_calibrationFlag", 1}});
        assert(generated.find(
                       "qinggan11_projected_fields=" + std::to_string(qinggan11_projected_fields) +
                       "U") != std::string::npos &&
               generated.find("qinggan11_settings_feedback_enabled=0U") != std::string::npos);
        const auto &fields = voyemu::catalog();
        const auto initial = voyemu::initial_state().at("raw_oem");
        unsigned mapped = 0;

        for (unsigned i = 0; i < 579; i++) {
            normal[i] = initial.at(fields[i].name).get<unsigned>();
            qinggan11_fields[i] = {65535, 0, normal[i]};
            const auto &row = source.at("fields").at(i);

            if (row.contains("wire")) {
                qinggan11_fields[i].bit = row.at("wire").at("start_bit");
                qinggan11_fields[i].width = row.at("wire").at("width");
                mapped++;
            }
        }

        assert(mapped == 56 && qinggan11_fields[37].bit == 69 &&
               qinggan11_fields[287].bit == 1111 && qinggan11_fields[288].bit == 1112 &&
               qinggan11_fields[290].bit == 1144 && qinggan11_fields[154].bit == 335 &&
               qinggan11_fields[154].width == 1);

        for (unsigned i = 0; i < 650; i++) {
            original_bytes[i] = static_cast<unsigned char>(i * 73 + 19);
        }

        qinggan11_settings.have = 1;
        std::memset(qinggan11_settings.bytes, 0xab, 10);
        unsigned char out[650];
        assert(dataParser_makedumydata(out, 650) == 650 && qinggan11_ack == offered);

        for (unsigned bit = 0; bit < 5200; bit++) {
            bool supported = bit == 465 || bit == 466;

            for (const auto &field : qinggan11_fields) {
                if (field.width && bit >= field.bit && bit < field.bit + field.width) {
                    supported = true;
                }
            }

            if (!supported) {
                assert(((out[bit / 8] ^ original_bytes[bit / 8]) & (1U << (bit % 8))) == 0);
            }
        }

        assert(((out[58] >> 1) & 3) == 1 && !std::memcmp(out + 250, original_bytes + 250, 10));
        assert(((out[335 / 8] >> (335 % 8)) & 1) == 1);
        offered++;
        normal[154] = 0;
        assert(dataParser_makedumydata(out, 650) == 650 && qinggan11_ack == offered &&
               ((out[335 / 8] >> (335 % 8)) & 1) == 0);
        offered++;
        normal[154] = 1;
        assert(dataParser_makedumydata(out, 650) == 650 && qinggan11_ack == offered &&
               ((out[335 / 8] >> (335 % 8)) & 1) == 1);
        // The model ignition reaches bit 0, the OEM power item of powermgr, in both states.
        assert(qinggan11_fields[0].bit == 0 && qinggan11_fields[0].width == 1 && (out[0] & 1) == 1);
        offered++;
        normal[0] = 0;
        assert(dataParser_makedumydata(out, 650) == 650 && qinggan11_ack == offered &&
               (out[0] & 1) == 0);
        offered++;
        normal[0] = 1;
        assert(dataParser_makedumydata(out, 650) == 650 && qinggan11_ack == offered &&
               (out[0] & 1) == 1);
        // Verify the packed bits independently of the table used by the projector.
        auto packed = [&](unsigned bit, unsigned width) {
            unsigned value = 0;

            for (unsigned i = 0; i < width; ++i) {
                value |= unsigned((out[(bit + i) / 8] >> ((bit + i) % 8)) & 1U) << i;
            }

            return value;
        };
        struct Case {
            std::string_view name;
            unsigned bit, width, value;
        };
        const Case added[] = {{"pos_lamp", 3, 1, 1},
                              {"low_beam", 4, 1, 1},
                              {"high_beam", 5, 1, 1},
                              {"rear_fog", 10, 1, 1},
                              {"bcm_DRLStatus", 13, 1, 1},
                              {"hood", 184, 1, 1},
                              {"door_fr", 186, 1, 1},
                              {"door_rl", 187, 1, 1},
                              {"door_rr", 188, 1, 1},
                              {"tail_gate", 189, 1, 1},
                              {"remain_mileage_e", 1090, 11, 280},
                              {"remain_mileage_f", 2218, 11, 420},
                              {"odo", 1120, 24, 12346},
                              {"fuel_percent", 1236, 7, 68},
                              {"actual_power", 1262, 7, 12},
                              {"allowed_power", 1255, 7, 80},
                              {"actual_power10x", 1407, 12, 120},
                              {"allowed_power10x", 1395, 12, 800},
                              {"vcu_remainingMileageReal", 1433, 11, 280},
                              {"vcu_remainingMileageCLTCWLTC", 1571, 11, 280},
                              {"remain_mileage_f_nedc", 1463, 10, 420},
                              {"remain_mileage_f_wltc", 2229, 11, 420}};

        for (const auto &signal : added) {
            const auto field = std::find_if(fields.begin(), fields.end(), [&](const auto &f) {
                return f.name == signal.name;
            });
            assert(field != fields.end());
            const auto position = static_cast<unsigned>(field - fields.begin());
            const auto previous = normal[position];
            normal[position] = signal.value;
            ++offered;
            assert(dataParser_makedumydata(out, 650) == 650 && qinggan11_ack == offered &&
                   packed(signal.bit, signal.width) == signal.value);
            normal[position] = previous;
            ++offered;
            assert(dataParser_makedumydata(out, 650) == 650 && qinggan11_ack == offered &&
                   packed(signal.bit, signal.width) == previous);
        }

        normal[286] = normal[373] = normal[404] = 1023;
        ++offered;
        assert(dataParser_makedumydata(out, 650) == 650 && qinggan11_ack == offered);
        assert(packed(2218, 11) == 2047 && packed(1463, 10) == 1023 && packed(2229, 11) == 2047);
        normal[286] = normal[373] = normal[404] = 0;
        offered++;
        bad = true;
        qinggan11_have = 0;
        assert(dataParser_makedumydata(out, 650) == 650 && qinggan11_ack == offered - 1 &&
               !std::memcmp(out, original_bytes, 650));
        enabled = false;
        const auto calls = rpc_calls;
        assert(dataParser_makedumydata(out, 650) == 650 && rpc_calls == calls &&
               !std::memcmp(out, original_bytes, 650));
        auto reject = [&](Json definition) {
            bool rejected = false;
            try {
                voyemu::qnx_qinggan11_mcu_layout(definition);
            } catch (...) {
                rejected = true;
            }

            assert(rejected);
        };
        auto wrong = source;
        wrong["hardware_adapter"] = "sa8155-qinggan11-731";
        reject(wrong);
        wrong = source;
        wrong["source_parser_sha256"] = std::string(64, '0');
        reject(wrong);
        wrong = source;
        wrong["fields"][37]["wire"]["start_bit"] = 0;
        reject(wrong);
        bool old_scope_rejected = false;
        try {
            voyemu::qnx_mcu_request_projection(650, 736, 19);
        } catch (...) {
            old_scope_rejected = true;
        }

        assert(old_scope_rejected);
        bool previous_scope_rejected = false;
        try {
            voyemu::qnx_mcu_request_projection(650, 736, 33);
        } catch (...) {
            previous_scope_rejected = true;
        }

        assert(previous_scope_rejected);
        const auto previous = voyemu::qnx_qinggan11_611_mcu_projection(
                voyemu::qnx_qinggan11_611_previous_projected_fields);
        assert(voyemu::qnx_mcu_request_projection(650, 736, 34) == previous);
        assert(!voyemu::qnx_mcu_delivery_scope(Json{{"mcu_projection", previous}},
                                               voyemu::qnx_qinggan11_611_mcu_projection()));
        const auto projection = voyemu::qnx_qinggan11_611_mcu_projection();
        assert(voyemu::qnx_mcu_request_projection(650, 736, 56) == projection);
        assert(voyemu::qnx_mcu_delivery_scope(Json{{"mcu_projection", projection}}, projection));
        assert(!voyemu::qnx_mcu_delivery_scope(Json{{"mcu_projection", projection}}, previous));
        assert(!voyemu::qnx_mcu_delivery_scope(Json{{"mcu_projection", projection}},
                                               voyemu::qnx_qinggan11_mcu_projection()));
        const Json metadata = {{"hardware_adapter", "sa8155-qinggan11-611"},
                               {"mcu_projection", projection}};
        assert(voyemu::qnx_runtime_layout("test-profile", metadata).projection == projection);
        auto foreign = metadata;
        foreign["hardware_adapter"] = "sa8155-qinggan11-731";
        bool rejected = false;
        try {
            voyemu::qnx_runtime_layout("test-profile", foreign);
        } catch (...) {
            rejected = true;
        }

        assert(rejected);
        assert(!projection.at("settings_feedback_supported").get<bool>() &&
               projection.at("unsupported_fields") == 523);
        std::cout
                << "Older qinggan11 adapter source56, stock controls, range sentinels, scoped RPC and unchanged unsupported bits passed\n";

        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';

        return 1;
    }
}
