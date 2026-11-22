#include "native_mcu_qinggan11.h"
#include "adas_ecu.hpp"
#include "model.hpp"
#include "qnx_qinggan11_mcu_layout.hpp"
#include <algorithm>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <future>
#include <iostream>
#include <mutex>
#include <thread>
using Json = voyemu::Json;
using u32 = unsigned;
using u64 = unsigned long long;
using usize = unsigned long;
static Qinggan11McuField qinggan11_fields[QINGGAN11_FIELDS];
static unsigned short qinggan11_wheel_fields[10];
static unsigned qinggan11_speed, qinggan11_speed10x, qinggan11_soc;
static unsigned qinggan11_calibration_fixture = 0, qinggan11_calibration_fixture_enabled = 0;
static unsigned qinggan11_projected_fields = 0, qinggan11_settings_feedback_enabled = 1;
#define QGFX_QINGGAN11_LAYOUT_DEFINED 1
static u32 normal[QINGGAN11_FIELDS];
// Catalogue positions of an unprojected field and of a two-bit projected field.
static unsigned unprojected_position, narrow_position;
static unsigned char original_bytes[650];
struct TestPacket {
    u32 fault, out_len;
    unsigned char data[3840];
};
static TestPacket storage{}, *packet = &storage;
static bool enabled = true;
static int original_result = 650;
static unsigned rpc_calls = 0, original_calls = 0, serial_calls = 0, bad = 0;
static u64 offered = 2, wheel_sequence = 0;
static unsigned wheel_index = 323, wheel_value = 1;

static void acquire() {}

static void release() {}

static void copy(void *d, const void *s, usize n) {
    std::memcpy(d, s, n);
}

static std::string trace;

static void emit(const char *line) {
    trace += line;
}

static bool async_mode = false, block_host = false, host_entered = false, host_released = false;
static std::mutex host_mutex;
static std::condition_variable host_cv;
static std::thread *worker = nullptr;

static char *test_getenv(const char *name) {
    const bool flag = !std::strcmp(name, "QGFX_NATIVE_MCU") ||
                      (async_mode && !std::strcmp(name, "QGFX_NATIVE_MCU_ASYNC"));

    return enabled && flag ? (char *)"1" : nullptr;
}

static int test_usleep(unsigned us) {
    std::this_thread::sleep_for(std::chrono::microseconds(us));

    return 0;
}

static int test_pthread_create(int *id, const void *, void *(*entry)(void *), void *arg) {
    assert(!worker);
    *id = 1;
    worker = new std::thread([=] {
        entry(arg);
    });

    return 0;
}

static int test_pthread_join(int id, void **) {
    assert(id == 1 && worker);
    worker->join();
    delete worker;
    worker = nullptr;

    return 0;
}

static int genuine(void *out, unsigned short size) {
    original_calls++;

    if (out && size >= 650) {
        std::memcpy(out, original_bytes, 650);
    }

    return original_result;
}

static int serializer_result = -7;

static int serializer(void *, void *, unsigned) {
    serial_calls++;

    return serializer_result;
}

static unsigned library_opens, symbol_lookups;

static void *test_dlopen(const char *, int) {
    ++library_opens;

    return (void *)1;
}

static void *test_dlsym(void *, const char *n) {
    ++symbol_lookups;

    return !std::strcmp(n, "dataParser_makedumydata") ? (void *)genuine : (void *)serializer;
}

static u64 call(u32, const u64 *, unsigned, const void *, unsigned);
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

static u64 call(u32 op, const u64 *args, unsigned count, const void *, unsigned) {
    assert(count == (op == 364 ? 4 : 1));

    if (op == 364 && block_host) {
        std::unique_lock lock(host_mutex);
        host_entered = true;
        host_cv.notify_all();
        host_cv.wait(lock, [] {
            return host_released;
        });
    }

    rpc_calls++;
    storage = {};

    if (op == 364) {
        assert(args[0] == qinggan11_ack && args[1] == 650 && args[2] == 736 &&
               args[3] == qinggan11_projected_fields);
        u32 values[QINGGAN11_FIELDS + 2] = {1, QINGGAN11_FIELDS};
        std::memcpy(values + 2, normal, sizeof normal);

        if (bad == 1) {
            values[0] = 0;
        }

        if (bad == 2) {
            values[1] = 579;
        }

        if (bad == 3) {
            values[2 + qinggan11_speed] = 256;
        }

        if (bad == 4) {
            values[2 + qinggan11_speed10x] = 1;
        }

        if (bad == 5) {
            values[2 + qinggan11_soc] = 101;
        }

        if (bad == 6) {
            values[2 + qinggan11_wheel_fields[0]] = 1;
        }

        if (bad == 7) {
            values[2 + unprojected_position] ^= 1;
        }

        if (bad == 8) {
            values[2 + narrow_position] = 4;
        }

        storage.out_len = sizeof values;
        std::memcpy(storage.data, values, sizeof values);

        if (bad == 9) {
            storage.fault = 1;
        }

        if (bad == 10) {
            storage.out_len--;
        }

        if (bad == 11) {
            storage.out_len = 0;
        }

        return bad == 11 ? 0xfeed0001 : bad == 12 ? 0x100000000ULL : offered;
    }

    assert(op == 363 && args[0] == qinggan11_wheel_ack);
    u32 values[] = {wheel_index, wheel_value};
    storage.out_len = sizeof values;
    std::memcpy(storage.data, values, sizeof values);

    return wheel_sequence;
}

static void reset() {
    trace.clear();
    std::memset(qinggan11_settings_trace, 0, sizeof qinggan11_settings_trace);
    qinggan11_settings = {};
    serializer_result = -7;
    serial_calls = 0;
    qinggan11_calibration_fixture = qinggan11_calibration_fixture_enabled = 0;
    qinggan11_ack = qinggan11_wheel_ack = 0;
    qinggan11_have = 0;
    qinggan11_rejects = 0;
    std::memset(qinggan11_values, 0, sizeof qinggan11_values);
    std::memset(qinggan11_wheel, 0, sizeof qinggan11_wheel);
    rpc_calls = original_calls = bad = 0;
    original_result = 650;
    enabled = true;
    offered = 2;
    wheel_sequence = 0;
    async_mode = block_host = host_entered = host_released = false;
    native_async_lock = native_async_stop = native_async_created = 0;
    native_async_mcu_staged = native_async_wheel_staged = 0;
}

// Independent pinned MidBitGet_intel contract: LE64 at bit/8, shift bit%8, mask width.
static unsigned field(const unsigned char *out, unsigned position) {
    const auto &f = qinggan11_fields[position];

    assert(f.width && f.width <= 32 && f.bit + f.width <= 5200);
    std::uint64_t word = 0;

    for (unsigned i = 0; i < 8 && f.bit / 8 + i < 650; i++) {
        word |= std::uint64_t(out[f.bit / 8 + i]) << (8 * i);
    }

    return unsigned((word >> (f.bit & 7)) & ((1ULL << f.width) - 1));
}

int main(int argc, char **argv) {
    try {
        assert(argc == 2);
        qinggan11_projected_fields =
                voyemu::qnx_adapter(voyemu::QnxGuestAdapter::sa8155_qinggan11_731)
                        .mcu_projected_fields;
        const auto root = std::filesystem::canonical(argv[1]);
        const auto source = voyemu::read_private_json(
                root / "src/guest/qnx/native_mcu_qinggan11.json", 512 * 1024);
        unsigned adapter_profiles = 0;

        // Every profile of this hardware adapter starts fresh with AEB on.
        for (const auto &entry : std::filesystem::directory_iterator(root / "profiles")) {
            const auto definition = entry.path() / "profile-qnx.json";

            if (!std::filesystem::is_regular_file(definition) ||
                voyemu::read_private_json(definition, 128 * 1024).at("hardware_adapter") !=
                        "sa8155-qinggan11-731") {
                continue;
            }

            const auto profile = voyemu::read_private_json(entry.path() / "profile.json",
                                                           1024 * 1024);
            const auto fresh = voyemu::select_vehicle_startup(
                    voyemu::VehicleStartup::fresh, voyemu::profile_vehicle_catalog(profile),
                    profile.at("initial_state"), nullptr);
            assert(fresh.at("raw_oem").at("aeb_sts") == 1);
            ++adapter_profiles;
        }

        assert(adapter_profiles > 0);
        const auto generated = voyemu::qnx_qinggan11_mcu_layout(source);
        assert(generated.find("{1228,8,0U}") != std::string::npos &&
               generated.find("qinggan11_fields[736]") != std::string::npos);
        const auto initial = voyemu::initial_state(voyemu::VehicleCatalog::qinggan11).at("raw_oem");
        const auto &catalog = voyemu::catalog(voyemu::VehicleCatalog::qinggan11);
        auto pos = [&](std::string_view name) {
            const auto at = std::find_if(catalog.begin(), catalog.end(), [&](const auto &f) {
                return f.name == name;
            });
            assert(at != catalog.end());

            return static_cast<unsigned>(at - catalog.begin());
        };
        unsigned mapped = 0;

        for (unsigned i = 0; i < QINGGAN11_FIELDS; i++) {
            const auto &row = source.at("fields").at(i);
            normal[i] = initial.at(catalog[i].name).get<unsigned>();
            qinggan11_fields[i] = {65535, 0, normal[i]};

            if (!row.contains("unprojected")) {
                qinggan11_fields[i].bit = row.at("wire").at("start_bit");
                qinggan11_fields[i].width = row.at("wire").at("width");
                mapped++;
            }
        }

        const char *buttons[] = {"swm_Photo",     "swm_menu",     "swm_OK",        "swm_upAdd",
                                 "swm_downMinus", "swm_leftLast", "swm_rightNext", "swm_nvsSwitch",
                                 "swm_phone",     "swm_voice"};

        for (unsigned i = 0; i < 10; i++) {
            qinggan11_wheel_fields[i] = static_cast<unsigned short>(pos(buttons[i]));
        }

        qinggan11_speed = pos("speed");
        qinggan11_speed10x = pos("speed10x");
        qinggan11_soc = pos("soc");
        unprojected_position = pos("set_byte0");
        narrow_position = pos("mpc_laneAssitType");
        const auto aeb = pos("aeb_sts"), speed = pos("speed"), speed10x = pos("speed10x"),
                   soc = pos("soc"), door = pos("door_fl"),
                   menu = static_cast<unsigned>(qinggan11_wheel_fields[1]);
        assert(generated.find("qinggan11_wheel_fields[10]={" + std::to_string(pos("swm_Photo")) +
                              ",") != std::string::npos);
        assert(qinggan11_fields[aeb].bit == 110 && qinggan11_fields[aeb].width == 3);
        assert(mapped == qinggan11_projected_fields && qinggan11_fields[speed].bit == 1228 &&
               qinggan11_fields[speed10x].bit == 1383 && qinggan11_fields[soc].bit == 1243);
        assert(qinggan11_fields[door].bit == 185 && qinggan11_fields[pos("door_fr")].bit == 186 &&
               qinggan11_fields[pos("door_rl")].bit == 187 &&
               qinggan11_fields[pos("door_rr")].bit == 188);
        // All ten wheel signals have wire bits, the photo and night vision ones included.
        assert(qinggan11_fields[menu].bit == 1286 &&
               qinggan11_fields[pos("swm_rightNext")].bit == 1291 &&
               qinggan11_fields[pos("swm_Photo")].bit == 1285 &&
               qinggan11_fields[pos("swm_nvsSwitch")].bit == 1292);
        // The settings feedback owns bytes 250..259 and a 34-bit reserved field stays unwritten.
        assert(!qinggan11_fields[pos("set_byte0")].width &&
               !qinggan11_fields[pos("reserved_03")].width &&
               !qinggan11_fields[pos("eps_calibrationFlag")].width);
        // OEM parser case 53 writes the input value to lightitems output bit 109.
        unsigned char adjacent[650] = {};

        for (unsigned value : {0U, 1U, 3U}) {
            for (unsigned i = 0; i < 650; i++) {
                original_bytes[i] = (unsigned char)(i * 73 + 41);
            }

            normal[aeb] = value;
            reset();
            unsigned char out[650];
            assert(dataParser_makedumydata(out, 650) == 650);
            assert(field(out, aeb) == value);

            if (value == 0) {
                std::memcpy(adjacent, out, 650);
            }

            for (unsigned bit : {109U, 113U}) {
                assert(((out[bit / 8] ^ adjacent[bit / 8]) & (1U << (bit % 8))) == 0);
            }

            const unsigned input = (out[13] >> 6 | (unsigned(out[14]) << 2)) & 7U;
            unsigned char light13 = 0x1f;
            light13 = (light13 & 0x1f) | (input << 5);
            assert((light13 >> 5) == value && (light13 & 0x1f) == 0x1f);
        }

        normal[aeb] = 0;
        unsigned char mask[650] = {};

        for (const auto &f : qinggan11_fields) {
            for (unsigned b = 0; b < f.width; b++) {
                mask[(f.bit + b) / 8] |= (unsigned char)(1U << ((f.bit + b) % 8));
            }
        }

        for (unsigned seed = 0; seed < 8; seed++) {
            for (unsigned i = 0; i < 650; i++) {
                original_bytes[i] = (unsigned char)(i * 73 + seed * 41);
            }

            reset();
            unsigned char out[650];
            assert(dataParser_makedumydata(out, 650) == 650 && qinggan11_ack == 2 &&
                   original_calls == 1);

            for (unsigned i = 0; i < 650; i++) {
                assert(((out[i] ^ original_bytes[i]) & ~mask[i]) == 0);
            }

            unsigned char tampered[650];
            std::memcpy(tampered, out, 650);
            bool detected = false;

            for (unsigned bit = 0; bit < 5200; bit++) {
                if (!(mask[bit / 8] & (1U << (bit % 8)))) {
                    tampered[bit / 8] ^= (unsigned char)(1U << (bit % 8));
                    break;
                }
            }

            for (unsigned i = 0; i < 650; i++) {
                if (((tampered[i] ^ original_bytes[i]) & ~mask[i]) != 0) {
                    detected = true;
                }
            }

            assert(detected);

            for (unsigned i = 0; i < QINGGAN11_FIELDS; i++) {
                if (qinggan11_fields[i].width) {
                    assert(field(out, i) == normal[i]);
                }
            }

            assert(field(out, soc) == 55 && field(out, speed) == 0);
            normal[speed] = 42;
            normal[speed10x] = 420;
            normal[door] = 1;
            // The model ignition reaches the OEM power items: off during a cluster cold boot.
            assert(qinggan11_fields[0].width == 1 && field(out, 0) == (seed ? 0U : 1U));
            normal[0] = 0;
            offered = 3;
            assert(dataParser_makedumydata(out, 650) == 650 && qinggan11_ack == 3 &&
                   field(out, speed) == 42 && field(out, speed10x) == 420 &&
                   field(out, door) == 1 && field(out, 0) == 0);
            wheel_sequence = 10;
            assert(dataParser_makedumydata(out, 650) == 650 && qinggan11_wheel_ack == 10 &&
                   field(out, menu) == 1);
            wheel_sequence = 11;
            wheel_value = 0;
            assert(dataParser_makedumydata(out, 650) == 650 && qinggan11_wheel_ack == 11 &&
                   field(out, menu) == 0);
            wheel_sequence = 12;
            wheel_index = 332;
            wheel_value = 1;
            assert(dataParser_makedumydata(out, 650) == 650 && qinggan11_wheel_ack == 11);
            normal[speed] = normal[speed10x] = normal[door] = 0;
            wheel_index = 323;
            wheel_value = 1;
            wheel_sequence = 0;
        }

        for (unsigned fail = 1; fail <= 12; fail++) {
            reset();
            bad = fail;
            unsigned char out[650];
            assert(dataParser_makedumydata(out, 650) == 650 && !qinggan11_ack && !qinggan11_have &&
                   std::memcmp(out, original_bytes, 650) == 0);
        }

        reset();
        unsigned char cached[650];
        assert(dataParser_makedumydata(cached, 650) == 650 && qinggan11_ack == 2);
        bad = 7;
        offered = 3;
        assert(dataParser_makedumydata(cached, 650) == 650 && qinggan11_ack == 2 &&
               field(cached, speed) == 0);

        for (int result : {-1, 0, 280, 649, 651}) {
            reset();
            original_result = result;
            unsigned char out[650];
            assert(dataParser_makedumydata(out, 650) == result && !rpc_calls && !qinggan11_ack);
        }

        reset();
        enabled = false;
        unsigned char out[650];
        assert(dataParser_makedumydata(out, 650) == 650 && !rpc_calls &&
               std::memcmp(out, original_bytes, 650) == 0);
        reset();
        assert(dataParser_makedumydata(out, 649) == 650 && !rpc_calls);
        assert(dataparser_makesoc2mcudata(nullptr, nullptr, 0) == -7 && serial_calls == 1);
        reset();
        bad = 11;
        unsigned char settings_tx[650] = {}, settings_rx[650];

        for (unsigned i = 0; i < 9; i++) {
            settings_tx[30 + i] = (unsigned char)(i * 7 + 3);
        }

        settings_tx[39] = 0xc5;

        for (unsigned i = 0; i < 9; i++) {
            settings_tx[39] ^= settings_tx[30 + i];
        }

        serializer_result = 0;
        assert(dataparser_makesoc2mcudata(nullptr, settings_tx, 39) == 0 &&
               !qinggan11_settings.have);
        assert(dataparser_makesoc2mcudata(nullptr, settings_tx, 40) == 0 &&
               qinggan11_settings.have);
        assert(dataParser_makedumydata(settings_rx, 650) == 650 && !qinggan11_have);

        for (unsigned i = 0; i < 650; i++) {
            assert(settings_rx[i] ==
                   (i >= 250 && i < 260 ? settings_tx[i - 220] : original_bytes[i]));
        }

        assert(trace.find("lane=0 sample=1 bytes=") != std::string::npos &&
               trace.find("lane=2 sample=1 bytes=") != std::string::npos);
        const auto first_trace = trace;
        assert(dataparser_makesoc2mcudata(nullptr, settings_tx, 650) == 0);
        assert(dataParser_makedumydata(settings_rx, 650) == 650);
        assert(trace == first_trace);

        for (unsigned sample = 1; sample <= 12; sample++) {
            settings_tx[30] = (unsigned char)sample;
            settings_tx[39] = 0xc5;

            for (unsigned i = 0; i < 9; i++) {
                settings_tx[39] ^= settings_tx[30 + i];
            }

            assert(dataparser_makesoc2mcudata(nullptr, settings_tx, 650) == 0);
            assert(dataParser_makedumydata(settings_rx, 650) == 650);
            assert(std::memcmp(settings_rx + 250, settings_tx + 30, 10) == 0);
        }

        assert(qinggan11_settings_trace[0].count == 8 && qinggan11_settings_trace[2].count == 8 &&
               trace.find("sample=9") == std::string::npos);
        const auto bounded_trace = trace;
        settings_tx[39] ^= 1;
        assert(dataparser_makesoc2mcudata(nullptr, settings_tx, 650) == 0);
        assert(trace == bounded_trace);
        settings_tx[39] ^= 1;
        const auto previous = qinggan11_settings;
        settings_tx[39] ^= 1;
        assert(dataparser_makesoc2mcudata(nullptr, settings_tx, 650) == 0 &&
               qinggan11_settings.have == previous.have &&
               std::memcmp(qinggan11_settings.bytes, previous.bytes, 10) == 0);
        serializer_result = -1;
        settings_tx[39] ^= 1;
        assert(dataparser_makesoc2mcudata(nullptr, settings_tx, 650) == -1);
        enabled = false;
        assert(dataParser_makedumydata(settings_rx, 650) == 650 &&
               std::memcmp(settings_rx, original_bytes, 650) == 0);

        for (unsigned mutation = 0; mutation < 8; mutation++) {
            auto invalid = source;

            switch (mutation) {
            case 0:
                invalid["source_parser_sha256"] = "bad";
                break;
            case 1:
                invalid["fields"][0]["wire"]["order"] = 0;
                break;
            case 2:
                invalid["fields"][0]["wire"]["start_bit"] = 5200;
                break;
            case 3:
                invalid["fields"][1]["wire"]["start_bit"] = 0;
                break;
            case 4:
                invalid["fields"][1]["wire"]["index"] = 1;
                break;
            case 5:
                invalid["fields"][0]["initial"] = 0;
                break;
            case 6:
                invalid["fields"][0]["wire"]["width"] = 33;
                break;
            case 7:
                invalid["fields"][0]["name"] = "wrong";
                break;
            default:
                break;
            }

            bool rejected = false;
            try {
                voyemu::qnx_qinggan11_mcu_layout(invalid);
            } catch (...) {
                rejected = true;
            }

            assert(rejected);
        }

        const Json simulated = {{"eps_calibrationFlag", 1}};
        assert(voyemu::qnx_qinggan11_mcu_layout(source, simulated)
                       .find("qinggan11_calibration_fixture=1U") != std::string::npos);
        assert(voyemu::qnx_qinggan11_mcu_layout(source).find("qinggan11_calibration_fixture=0U") !=
               std::string::npos);

        for (const auto &invalid : Json::array({nullptr,
                                                Json::array(),
                                                {{"eps_calibrationFlag", -1}},
                                                {{"eps_calibrationFlag", 4}},
                                                {{"eps_calibrationFlag", 1.0}},
                                                {{"eps_calibrationFlag", "1"}},
                                                {{"eps_fault", 1}},
                                                {{"eps_calibrationFlag", 1}, {"extra", 1}}})) {
            bool rejected = false;
            try {
                voyemu::qnx_qinggan11_mcu_layout(source, invalid);
            } catch (...) {
                rejected = true;
            }

            assert(rejected);
        }

        // The calibration fixture owns eps_calibrationFlag, the settings feedback bytes 250..259,
        // and a field wider than a guest value stays unwritten.
        const auto fixture_row = pos("eps_calibrationFlag");

        for (unsigned mutation = 0; mutation < 6; ++mutation) {
            auto invalid = source;

            if (mutation == 0) {
                invalid["fields"][fixture_row]["unprojected"] = "settings_feedback";
            }

            if (mutation == 1) {
                invalid["fields"][fixture_row].erase("unprojected");
            }

            if (mutation == 2) {
                invalid["fields"][pos("set_byte0")].erase("unprojected");
            }

            if (mutation == 3) {
                invalid["hardware_adapter"] = "sa8155-qinggan09";
            }

            if (mutation == 4) {
                invalid["fields"][pos("reserved_04")].erase("unprojected");
            }

            if (mutation == 5) {
                invalid["fields"][pos("speed")]["unprojected"] = "wider_than_guest_value";
            }

            bool rejected = false;
            try {
                voyemu::qnx_qinggan11_mcu_layout(invalid, simulated);
            } catch (...) {
                rejected = true;
            }

            assert(rejected);
        }

        for (unsigned value = 0; value < 4; ++value) {
            assert(voyemu::qnx_qinggan11_mcu_layout(source, Json{{"eps_calibrationFlag", value}})
                           .find("qinggan11_calibration_fixture=" + std::to_string(value) + "U") !=
                   std::string::npos);
        }

        for (unsigned seed = 0; seed < 256; ++seed) {
            for (unsigned i = 0; i < 650; ++i) {
                original_bytes[i] = (unsigned char)(i * 73 + seed * 41);
            }

            reset();
            unsigned char baseline[650], fixture[650];
            assert(dataParser_makedumydata(baseline, 650) == 650);

            for (unsigned value = 0; value < 4; ++value) {
                reset();
                qinggan11_calibration_fixture_enabled = 1;
                qinggan11_calibration_fixture = value;
                assert(dataParser_makedumydata(fixture, 650) == 650 && qinggan11_ack == 2);
                assert(((fixture[58] >> 1) & 3) == value);

                for (unsigned i = 0; i < 650; ++i) {
                    assert(((baseline[i] ^ fixture[i]) & ~(i == 58 ? 6U : 0U)) == 0);
                }
            }

            reset();
            qinggan11_calibration_fixture_enabled = 1;
            qinggan11_calibration_fixture = 1;
            bad = 7;
            assert(dataParser_makedumydata(fixture, 650) == 650 && !qinggan11_ack &&
                   std::memcmp(fixture, original_bytes, 650) == 0);
            reset();
            qinggan11_calibration_fixture_enabled = 1;
            qinggan11_calibration_fixture = 1;
            enabled = false;
            assert(dataParser_makedumydata(fixture, 650) == 650 &&
                   std::memcmp(fixture, original_bytes, 650) == 0);
        }

        // Asynchronous transport: a delayed host RPC never holds the original MCU_COM or
        // RPCD_MAIN thread; they pack the staged revision, which is acknowledged on the next poll.
        {
            for (unsigned i = 0; i < 650; i++) {
                original_bytes[i] = (unsigned char)(i * 73 + 41);
            }

            reset();
            async_mode = block_host = true;
            native_async_start();
            {
                std::unique_lock lock(host_mutex);
                assert(host_cv.wait_for(lock, std::chrono::seconds(3), [] {
                    return host_entered;
                }));
            }
            unsigned char frame[650], settings[650];
            const auto calls = rpc_calls;
            auto consumer = std::async(std::launch::async, [&] {
                return dataParser_makedumydata(frame, 650) == 650 &&
                       dataparser_makesoc2mcudata(nullptr, settings, sizeof settings) == -7;
            });
            assert(consumer.wait_for(std::chrono::milliseconds(500)) == std::future_status::ready);
            assert(consumer.get() && rpc_calls == calls && !qinggan11_ack &&
                   std::memcmp(frame, original_bytes, 650) == 0);
            {
                std::lock_guard lock(host_mutex);
                host_released = true;
            }
            host_cv.notify_all();
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);

            while (__atomic_load_n(&native_async_mcu_staged, __ATOMIC_ACQUIRE) != 2) {
                assert(std::chrono::steady_clock::now() < deadline);
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }

            assert(!qinggan11_ack && dataParser_makedumydata(frame, 650) == 650 &&
                   qinggan11_ack == 2 && field(frame, speed) == normal[speed]);
            native_async_shutdown();
            assert(!worker);

            // The worker stops while a host RPC is pending, and stages nothing after it.
            reset();
            async_mode = block_host = true;
            native_async_start();
            {
                std::unique_lock lock(host_mutex);
                assert(host_cv.wait_for(lock, std::chrono::seconds(3), [] {
                    return host_entered;
                }));
            }
            auto stop = std::async(std::launch::async, [] {
                native_async_shutdown();
            });

            while (!__atomic_load_n(&native_async_stop, __ATOMIC_ACQUIRE)) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }

            {
                std::lock_guard lock(host_mutex);
                host_released = true;
            }
            host_cv.notify_all();
            assert(stop.wait_for(std::chrono::seconds(3)) == std::future_status::ready);
            stop.get();
            assert(!worker && !native_async_mcu_staged && !qinggan11_ack);

            // An invalid revision is rejected by the worker and never staged.
            reset();
            async_mode = true;
            bad = 7;
            native_async_poll();
            assert(!native_async_mcu_staged && !qinggan11_have && qinggan11_rejects == 1);
            assert(dataParser_makedumydata(frame, 650) == 650 && !qinggan11_ack &&
                   std::memcmp(frame, original_bytes, 650) == 0);

            // A revision is fetched again only after the consumer packed the staged one.
            reset();
            async_mode = true;
            native_async_poll();
            const auto fetched = rpc_calls;
            native_async_poll();
            assert(native_async_mcu_staged == 2 && rpc_calls == fetched && !qinggan11_ack);
            assert(dataParser_makedumydata(frame, 650) == 650 && qinggan11_ack == 2);
        }

        // Each written field reaches its start bit and width of the OEM 7.3.1 parser descriptor.
        struct Case {
            std::string_view name;
            unsigned bit, width, value;
        };
        auto packed = [](const unsigned char *frame, unsigned bit, unsigned width) {
            unsigned value = 0;

            for (unsigned i = 0; i < width; ++i) {
                value |= unsigned((frame[(bit + i) / 8] >> ((bit + i) % 8)) & 1U) << i;
            }

            return value;
        };
        auto deliver_cases = [&](std::initializer_list<Case> cases) {
            for (const Case &signal : cases) {
                const auto position = pos(signal.name);
                const auto previous = normal[position];
                normal[position] = signal.value;
                reset();
                unsigned char frame[650];
                assert(dataParser_makedumydata(frame, 650) == 650 && qinggan11_ack == offered &&
                       packed(frame, signal.bit, signal.width) == signal.value);
                normal[position] = previous;
            }
        };
        // Fields that reach the OEM ADAS, APA/PDC and power manager items or that no parser case of
        // this firmware reads.
        deliver_cases({
                {"gsm_warnInformation_bak", 205, 2, 3},
                {"can_v1_5_bcm_tpmsLFSts", 227, 3, 5},
                {"can_v1_5_bcm_tpmsRFSts", 246, 3, 5},
                {"can_v1_5_bcm_tpmsLRSts", 265, 3, 5},
                {"can_v1_5_bcm_tpmsRRSts", 284, 3, 5},
                {"tbox_vehicleMode", 1449, 4, 11},
                {"apa_WarningReq", 1527, 1, 1},
                {"APA_PDCSysSts", 1528, 2, 3},
                {"APA_PDCFailureWarning", 1530, 1, 1},
                {"APA_PDCAudibleFrontBeepRate", 1531, 3, 5},
                {"APA_PDCAudibleRearBeepRate", 1534, 3, 5},
                {"pdc_sysSts", 1537, 2, 3},
                {"pdc_failureWarning", 1539, 1, 1},
                {"pdc_audibleFrontBeepRate", 1540, 3, 5},
                {"pdc_audibleRearBeepRate", 1543, 3, 5},
                {"apa_AbortWarningReq", 1598, 1, 1},
                {"apa_ActiveWarningReq", 1599, 1, 1},
                {"cfg_reserve_14", 2192, 8, 171},
                {"acc_mode", 80, 3, 2},
                {"mpc_icaSts", 83, 3, 2},
                {"mpc_isa_speed_limit", 96, 6, 12},
                {"mpc_isaOnOffSts", 102, 2, 1},
                {"isa_limit_warning", 104, 1, 1},
                {"left_visualization", 455, 2, 2},
                {"right_visualization", 457, 2, 2},
                {"time_gap_set", 692, 3, 3},
                {"SeatBeatFlash", 1548, 1, 1},
        });
        // The wire fields that only the qinggan11 catalogue has, one list per group.
        // ADAS lanes, targets and assistance states.
        deliver_cases({
                {"ADCU_ANP_TxtInfo", 86, 8, 171},
                {"ADCU_WarningDis", 108, 2, 3},
                {"mpc_lasSts", 116, 3, 7},
                {"Elk_SysSts", 168, 3, 7},
                {"Elk_Warningtype_L", 171, 3, 7},
                {"Elk_Warningtype_R", 174, 3, 7},
                {"TSR_Textinfo", 303, 5, 11},
                {"ADCU_LeTargetPosition", 444, 2, 3},
                {"ADCU_RiTargetPosition", 446, 2, 3},
                {"ADCU_LeRoadColorFct", 448, 2, 3},
                {"ADCU_RiRoadColorFct", 450, 2, 3},
                {"ADCU_LeLineType", 470, 4, 11},
                {"ADCU_RiLineType", 474, 4, 11},
                {"ADCU_LLeLineType", 478, 4, 11},
                {"ADCU_RRiLineType", 482, 4, 11},
                {"ADCU_LeLineDst", 486, 10, 683},
                {"ADCU_RiLineDst", 496, 10, 683},
                {"ADCU_LeLeLineDst", 506, 11, 683},
                {"ADCU_RiRiLineDst", 517, 11, 683},
                {"ADCU_LeLineColorFct", 528, 3, 7},
                {"ADCU_RiLineColorFct", 531, 3, 7},
                {"ADCU_LLeLineColorFct", 534, 3, 7},
                {"ADCU_RRiLineColorFct", 537, 3, 7},
                {"ADCU_Lane_curvatureRate", 547, 12, 2731},
                {"ADCU_Lane_C1", 559, 9, 171},
                {"FCTA_SwSt", 568, 1, 1},
                {"Rear_mrr_systemErrorSts", 569, 1, 1},
                {"Front_mrr_systemErrorSts", 570, 1, 1},
                {"mpc_tjaSts", 571, 3, 7},
                {"mpc_adasTakeoverReq", 574, 2, 3},
                {"ELK_Mainswitch", 579, 2, 3},
                {"ESA_Mainswitch", 581, 2, 3},
                {"mpc_LaneAssistOnOffSts", 583, 2, 3},
                {"ADCU_Lane_Radius", 585, 10, 683},
                {"Elk_SysS_Feedback", 595, 2, 3},
                {"ESA_SysS_Feedback", 597, 2, 3},
                {"SWS_PilotSW", 600, 1, 1},
                {"ADCU_VsetDis", 683, 8, 171},
                {"ADCU_timeGapdisReq", 691, 1, 1},
                {"ADCU_OverrideSts", 695, 1, 1},
                {"Angular_radar_status_display", 726, 2, 3},
                {"Angle_radar_alarm", 753, 2, 3},
                {"RCTA_SwSt", 755, 1, 1},
                {"RCW_SwSt", 756, 1, 1},
                {"DOW_SwSt", 757, 1, 1},
                {"LCDA_SwSt", 758, 1, 1},
                {"ADCU_GLA_Left_Sts", 1166, 3, 7},
                {"ADCU_GLA_Stright_Sts", 1169, 4, 11},
                {"ADCU_GLA_Right_Sts", 1173, 3, 7},
                {"ADCU_HANPMode", 1278, 3, 7},
                {"mpc_AcousticWarningReq", 1431, 2, 3},
                {"ADCU_ANPSoundRmd", 1655, 4, 11},
                {"ADCU_PlanLineSts", 1659, 2, 3},
                {"ADCU_ICAandANP_Animation_Display", 1661, 2, 3},
                {"ADCU_ISLCStatus", 1663, 4, 11},
                {"ADCU_ISLCSpdCtlLamp", 1667, 2, 3},
                {"mrr_accState1", 1669, 2, 3},
                {"mrr_accState2", 1671, 2, 3},
                {"ADCU_FVSRStatus", 1674, 3, 7},
                {"ADCU_ALCStatus", 1677, 3, 7},
                {"ADCU_PLCFuncSts", 1680, 3, 7},
                {"ADCU_GLAStatus", 1683, 2, 3},
                {"HMI_LED_Enable", 1685, 1, 1},
                {"ESA_status", 1686, 3, 7},
                {"TSR_SysSts", 1689, 3, 7},
                {"ADCU_RippleDis", 1692, 3, 7},
                {"ADCU_ALC_LLCArrow", 1695, 2, 3},
                {"ADCU_ALC_RiLCArrow", 1697, 2, 3},
                {"ADCU_LeLineSts", 1699, 2, 3},
                {"ADCU_RiLineSts", 1701, 2, 3},
                {"ADCU_TargetType_1", 2240, 3, 7},
                {"ADCU_TargetColor_1", 2243, 3, 7},
                {"ADCU_LateralDist_1", 2246, 7, 43},
                {"ADCU_LongitudinalDist_1", 2253, 9, 171},
                {"ADCU_LateralDistFlag_1", 2262, 1, 1},
                {"ADCU_TargetAngle_1", 2263, 3, 7},
                {"ADCU_TargetType_2", 2266, 3, 7},
                {"ADCU_TargetColor_2", 2269, 3, 7},
                {"ADCU_LateralDist_2", 2272, 7, 43},
                {"ADCU_LongitudinalDist_2", 2279, 9, 171},
                {"ADCU_LateralDistFlag_2", 2288, 1, 1},
                {"ADCU_TargetAngle_2", 2289, 3, 7},
                {"ADCU_TargetType_3", 2304, 3, 7},
                {"ADCU_TargetColor_3", 2307, 3, 7},
                {"ADCU_LateralDist_3", 2310, 7, 43},
                {"ADCU_LongitudinalDist_3", 2317, 9, 171},
                {"ADCU_LateralDistFlag_3", 2326, 1, 1},
                {"ADCU_TargetAngle_3", 2327, 3, 7},
                {"ADCU_TargetType_4", 2330, 3, 7},
                {"ADCU_TargetColor_4", 2333, 3, 7},
                {"ADCU_LateralDist_4", 2336, 7, 43},
                {"ADCU_LongitudinalDist_4", 2343, 9, 171},
                {"ADCU_LateralDistFlag_4", 2352, 1, 1},
                {"ADCU_TargetAngle_4", 2353, 3, 7},
                {"ADCU_TargetType_5", 2368, 3, 7},
                {"ADCU_TargetColor_5", 2371, 3, 7},
                {"ADCU_LateralDist_5", 2374, 7, 43},
                {"ADCU_LongitudinalDist_5", 2381, 9, 171},
                {"ADCU_LateralDistFlag_5", 2390, 1, 1},
                {"ADCU_TargetAngle_5", 2391, 3, 7},
                {"ADCU_TargetType_6", 2394, 3, 7},
                {"ADCU_TargetColor_6", 2397, 3, 7},
                {"ADCU_LateralDist_6", 2400, 7, 43},
                {"ADCU_LongitudinalDist_6", 2407, 9, 171},
                {"ADCU_LateralDistFlag_6", 2416, 1, 1},
                {"ADCU_TargetAngle_6", 2417, 3, 7},
                {"ADCU_TargetType_7", 2432, 3, 7},
                {"ADCU_TargetColor_7", 2435, 3, 7},
                {"ADCU_LateralDist_7", 2438, 7, 43},
                {"ADCU_LongitudinalDist_7", 2445, 9, 171},
                {"ADCU_LateralDistFlag_7", 2454, 1, 1},
                {"ADCU_TargetAngle_7", 2455, 3, 7},
                {"ADCU_TargetType_8", 2458, 3, 7},
                {"ADCU_TargetColor_8", 2461, 3, 7},
                {"ADCU_LateralDist_8", 2464, 7, 43},
                {"ADCU_LongitudinalDist_8", 2471, 9, 171},
                {"ADCU_LateralDistFlag_8", 2480, 1, 1},
                {"ADCU_TargetAngle_8", 2481, 3, 7},
                {"ADCU_TargetType_9", 2496, 3, 7},
                {"ADCU_TargetColor_9", 2499, 3, 7},
                {"ADCU_LateralDist_9", 2502, 7, 43},
                {"ADCU_LongitudinalDist_9", 2509, 9, 171},
                {"ADCU_LateralDistFlag_9", 2518, 1, 1},
                {"ADCU_TargetAngle_9", 2519, 3, 7},
                {"ADCU_TargetType_10", 2522, 3, 7},
                {"ADCU_TargetColor_10", 2525, 3, 7},
                {"ADCU_LateralDist_10", 2528, 7, 43},
                {"ADCU_LongitudinalDist_10", 2535, 9, 171},
                {"ADCU_LateralDistFlag_10", 2544, 1, 1},
                {"ADCU_TargetAngle_10", 2545, 3, 7},
                {"ADCU_TargetType_11", 2560, 3, 7},
                {"ADCU_TargetColor_11", 2563, 3, 7},
                {"ADCU_LateralDist_11", 2566, 7, 43},
                {"ADCU_LongitudinalDist_11", 2573, 9, 171},
                {"ADCU_LateralDistFlag_11", 2582, 1, 1},
                {"ADCU_TargetAngle_11", 2583, 3, 7},
                {"ADCU_TargetType_12", 2586, 3, 7},
                {"ADCU_TargetColor_12", 2589, 3, 7},
                {"ADCU_LateralDist_12", 2592, 7, 43},
                {"ADCU_LongitudinalDist_12", 2599, 9, 171},
                {"ADCU_LateralDistFlag_12", 2608, 1, 1},
                {"ADCU_TargetAngle_12", 2609, 3, 7},
                {"ADCU_TargetType_13", 2624, 2, 3},
                {"ADCU_LateralDist_13", 2626, 7, 43},
                {"ADCU_LongitudinalDist_13", 2633, 9, 171},
                {"ADCU_LateralDistFlag_13", 2642, 1, 1},
                {"ADCU_TargetType_14", 2643, 2, 3},
                {"ADCU_LateralDist_14", 2645, 7, 43},
                {"ADCU_LongitudinalDist_14", 2652, 9, 171},
                {"ADCU_LateralDistFlag_14", 2661, 1, 1},
                {"ADCU_TargetType_15", 2688, 2, 3},
                {"ADCU_LateralDist_15", 2690, 7, 43},
                {"ADCU_LongitudinalDist_15", 2697, 9, 171},
                {"ADCU_LateralDistFlag_15", 2706, 1, 1},
                {"ADCU_TargetType_16", 2707, 2, 3},
                {"ADCU_LateralDist_16", 2709, 7, 43},
                {"ADCU_LongitudinalDist_16", 2716, 9, 171},
                {"ADCU_LateralDistFlag_16", 2725, 1, 1},
                {"ADCU_TargetType_17", 2752, 2, 3},
                {"ADCU_LateralDist_17", 2754, 7, 43},
                {"ADCU_LongitudinalDist_17", 2761, 9, 171},
                {"ADCU_LateralDistFlag_17", 2770, 1, 1},
                {"ADCU_TargetType_18", 2771, 2, 3},
                {"ADCU_LateralDist_18", 2773, 7, 43},
                {"ADCU_LongitudinalDist_18", 2780, 9, 171},
                {"ADCU_LateralDistFlag_18", 2789, 1, 1},
        });
        // Driver monitoring and multimedia camera settings.
        deliver_cases({
                {"ivi_dvrPhotoReceive", 1673, 1, 1},
                {"ivi_dmsCameraCtl", 1724, 2, 3},
                {"ivi_dmsWearySet", 1726, 2, 3},
                {"ivi_dmsSenSet", 1728, 2, 3},
                {"ivi_roaCameraCtl", 1730, 1, 1},
                {"ivi_cameraChange", 1731, 3, 7},
                {"ivi_dmsWearyAssisSet", 1734, 2, 3},
                {"ivi_faceIDSet", 1736, 2, 3},
                {"ivi_gestureIDSet", 1738, 2, 3},
                {"ivi_dmsFaceIDRegiReq", 1740, 2, 3},
                {"ivi_FaceIDLoginReq", 1742, 2, 3},
                {"ivi_dmsRegiFaceID", 1744, 4, 11},
                {"ivi_dmsDeleteFaceID", 1748, 4, 11},
                {"ivi_FaceIDEmptydataResp", 1752, 1, 1},
        });
        // Body, steering, powertrain and range states.
        deliver_cases({
                {"EMS_GPFLmp", 9, 1, 1},
                {"bcm_rsm_lightSWReason", 461, 3, 7},
                {"break_warning_pop", 464, 1, 1},
                {"reserved_05", 467, 3, 7},
                {"reserved_06", 540, 7, 43},
                {"VCU_textinfo", 576, 3, 7},
                {"esc_cdpActive", 599, 1, 1},
                {"SOC_Calibrated_Flag", 601, 1, 1},
                {"eps_sasSteeringAngleValid", 744, 1, 1},
                {"remain_mileage_f_old", 1101, 10, 683},
                {"Trailer_EnabState", 1582, 3, 7},
                {"remain_mileage_f_wltc_reserved", 1585, 10, 683},
                {"Driver_SeatBeltSts", 1703, 1, 1},
                {"bcm_doorFLLockSts", 1704, 1, 1},
                {"bcm_antiTheftSts", 1705, 3, 7},
                {"eps_sasSteeringAngle", 1708, 16, 43691},
                {"bcm_RLReadLightSts", 1753, 1, 1},
                {"bcm_RRReadLightSts", 1754, 1, 1},
                {"vcu_ready", 1755, 1, 1},
                {"VCU_SOCmodesetnew", 1798, 3, 7},
        });
        // CAN message timeouts.
        deliver_cases({
                {"can_msg_timeout_346", 1878, 2, 3}, {"can_msg_timeout_379", 1888, 2, 3},
                {"can_msg_timeout_381", 1890, 2, 3}, {"can_msg_timeout_10d", 1942, 2, 3},
                {"can_msg_timeout_204", 1950, 2, 3}, {"can_msg_timeout_205", 1952, 2, 3},
                {"can_msg_timeout_15E", 1954, 2, 3}, {"can_msg_timeout_206", 1966, 2, 3},
                {"can_msg_timeout_268", 1968, 2, 3}, {"can_msg_timeout_269", 1970, 2, 3},
                {"can_msg_timeout_2fb", 1972, 2, 3}, {"can_msg_timeout_308", 1974, 2, 3},
                {"can_msg_timeout_36f", 1976, 2, 3}, {"can_msg_timeout_370", 1978, 2, 3},
                {"can_msg_timeout_371", 1980, 2, 3}, {"can_msg_timeout_372", 1982, 2, 3},
                {"can_msg_timeout_373", 1984, 2, 3}, {"can_msg_timeout_375", 1986, 2, 3},
                {"can_msg_timeout_376", 1988, 2, 3}, {"can_msg_timeout_377", 1990, 2, 3},
                {"can_msg_timeout_378", 1992, 2, 3}, {"can_msg_timeout_395", 1994, 2, 3},
                {"can_msg_timeout_37A", 1996, 2, 3}, {"can_msg_timeout_37B", 1998, 2, 3},
        });
        // Vehicle configuration.
        deliver_cases({
                {"cfg_reserve_11", 2172, 4, 11},
                {"cfg_steer_heat", 2176, 2, 3},
                {"cfg_rearseat_heat", 2178, 2, 3},
                {"cfg_smart_drive", 2180, 2, 3},
                {"cfg_battery_pack", 2182, 2, 3},
        });

        // AdasEcu acc and cancel write acc_mode, mpc_icaSts and time_gap_set, which reach their OEM
        // ADAS items, and the set speed ADCU_VsetDis in km/h. A settings byte, which the
        // settings feedback owns, makes the guest reject the logical revision as a whole, and the
        // host state for the guest carries its initial value beside the new speed and ADAS states.
        {
            auto state =
                    voyemu::apply_patch(voyemu::initial_state(voyemu::VehicleCatalog::qinggan11),
                                        {{"speed", 80}, {"gear", 3}, {"gear_valid", 1}});
            voyemu::AdasEcu adas;
            const auto now = voyemu::AdasEcu::Clock::now();
            std::uint32_t sequence = 0;
            auto press = [&](const char *button) {
                const bool pressed =
                        adas.input(state, ++sequence, button, "down", "test", now).handled;
                assert(pressed);
                const auto outcome = adas.input(state, ++sequence, button, "up", "test", now);
                state = voyemu::apply_patch(state, outcome.patch);
            };
            press("acc");
            assert(state.at("raw_oem").at("acc_mode") == 3);
            press("cancel");
            state = voyemu::apply_patch(state, {{"speed", 60}});
            const auto &raw = state.at("raw_oem");
            assert(raw.at("acc_mode") == 0 && raw.at("mpc_icaSts") == 1 &&
                   raw.at("ADCU_VsetDis") == 80 && raw.at("time_gap_set") == 1);
            auto deliver = [&](const Json &values) {
                for (unsigned i = 0; i < QINGGAN11_FIELDS; i++) {
                    normal[i] = values.at(catalog[i].name).get<unsigned>();
                }

                reset();
                unsigned char frame[650];
                const bool accepted = dataParser_makedumydata(frame, 650) == 650 &&
                                      qinggan11_ack == offered;

                return accepted && field(frame, speed) == 60;
            };
            assert(deliver(raw) && !qinggan11_rejects);
            const auto settings_state = voyemu::apply_patch(state, {{"set_byte0", 5}});
            const auto &settings_raw = settings_state.at("raw_oem");
            assert(!deliver(settings_raw) && qinggan11_rejects == 1);
            const auto guest = voyemu::qnx_qinggan11_guest_state(source, settings_raw);
            assert(guest.at("acc_mode") == 0 && guest.at("mpc_icaSts") == 1 &&
                   guest.at("set_byte0") == 0 && guest.at("time_gap_set") == 1 &&
                   guest.at("ivi_languageSet") == raw.at("ivi_languageSet"));
            assert(deliver(guest) && !qinggan11_rejects);

            for (unsigned i = 0; i < QINGGAN11_FIELDS; i++) {
                normal[i] = initial.at(catalog[i].name).get<unsigned>();
            }
        }

        // The originals are resolved once per process: a per-frame dlopen leaves a loader
        // handle behind that every later dlsym walks, slowing the OEM threads without bound.
        {
            reset();
            unsigned char frame[650], settings[650];
            const unsigned opened = library_opens, resolved = symbol_lookups;

            for (unsigned i = 0; i < 1000; ++i) {
                dataParser_makedumydata(frame, 650);
                dataparser_makesoc2mcudata(nullptr, settings, sizeof settings);
            }

            assert(opened <= 1 && resolved <= 2 && library_opens == opened &&
                   symbol_lookups == resolved);
        }

        std::cout
                << "qinggan11 adapter genuine650 projection, named fields, original-bit preservation, vehicle/wheel ACK/error guards passed\n";

        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';

        return 1;
    }
}
