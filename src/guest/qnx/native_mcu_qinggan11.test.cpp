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
static Qinggan11McuField qinggan11_fields[579];
static unsigned qinggan11_calibration_fixture = 0, qinggan11_calibration_fixture_enabled = 0;
static unsigned qinggan11_projected_fields = 0, qinggan11_settings_feedback_enabled = 1;
#define QGFX_QINGGAN11_LAYOUT_DEFINED 1
#include "mcu_catalog.inc"
static u32 normal[579];
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
        u32 values[581] = {1, 579};
        std::memcpy(values + 2, normal, sizeof normal);

        if (bad == 1) {
            values[0] = 0;
        }

        if (bad == 2) {
            values[1] = 736;
        }

        if (bad == 3) {
            values[312] = 256;
        }

        if (bad == 4) {
            values[360] = 1;
        }

        if (bad == 5) {
            values[314] = 101;
        }

        if (bad == 6) {
            values[323] = 1;
        }

        if (bad == 7) {
            values[2 + 6] ^= 1;
        }

        if (bad == 8) {
            values[2 + 181] = 4;
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
                root / "src/guest/qnx/native_mcu_qinggan11.json", 256 * 1024);
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
            const auto fresh = voyemu::select_vehicle_startup(voyemu::VehicleStartup::fresh,
                                                              profile.at("initial_state"), nullptr);
            assert(fresh.at("raw_oem").at("aeb_sts") == 1);
            ++adapter_profiles;
        }

        assert(adapter_profiles > 0);
        const auto generated = voyemu::qnx_qinggan11_mcu_layout(source);
        assert(generated.find("{1228,8,0U,0U,0U}") != std::string::npos);
        const auto initial = voyemu::initial_state().at("raw_oem");
        const auto &catalog = voyemu::catalog();
        unsigned mapped = 0;

        for (unsigned i = 0; i < 579; i++) {
            const auto &row = source.at("fields").at(i);
            normal[i] = initial.at(catalog[i].name).get<unsigned>();
            qinggan11_fields[i] = {65535, 0, normal[i]};

            if (row.contains("wire")) {
                qinggan11_fields[i].bit = row.at("wire").at("start_bit");
                qinggan11_fields[i].width = row.at("wire").at("width");
                mapped++;
            }
        }

        assert(qinggan11_fields[52].bit == 110 && qinggan11_fields[52].width == 3);
        assert(mapped == qinggan11_projected_fields && qinggan11_fields[310].bit == 1228 &&
               qinggan11_fields[358].bit == 1383 && qinggan11_fields[312].bit == 1243);
        assert(qinggan11_fields[104].bit == 185 && qinggan11_fields[105].bit == 186 &&
               qinggan11_fields[106].bit == 187 && qinggan11_fields[107].bit == 188);
        // All ten wheel signals have wire bits, the photo and night vision ones included.
        assert(qinggan11_fields[322].bit == 1286 && qinggan11_fields[327].bit == 1291 &&
               qinggan11_fields[321].bit == 1285 && qinggan11_fields[328].bit == 1292);
        // OEM parser case 53 writes the input value to lightitems output bit 109.
        unsigned char adjacent[650] = {};

        for (unsigned value : {0U, 1U, 3U}) {
            for (unsigned i = 0; i < 650; i++) {
                original_bytes[i] = (unsigned char)(i * 73 + 41);
            }

            normal[52] = value;
            reset();
            unsigned char out[650];
            assert(dataParser_makedumydata(out, 650) == 650);
            assert(field(out, 52) == value);

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

        normal[52] = 0;
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

            for (unsigned i = 0; i < 579; i++) {
                if (qinggan11_fields[i].width) {
                    assert(field(out, i) == normal[i]);
                }
            }

            assert(field(out, 312) == 55 && field(out, 310) == 0);
            normal[310] = 42;
            normal[358] = 420;
            normal[104] = 1;
            // The model ignition reaches the OEM power items: off during a cluster cold boot.
            assert(qinggan11_fields[0].width == 1 && field(out, 0) == (seed ? 0U : 1U));
            normal[0] = 0;
            offered = 3;
            assert(dataParser_makedumydata(out, 650) == 650 && qinggan11_ack == 3 &&
                   field(out, 310) == 42 && field(out, 358) == 420 && field(out, 104) == 1 &&
                   field(out, 0) == 0);
            wheel_sequence = 10;
            assert(dataParser_makedumydata(out, 650) == 650 && qinggan11_wheel_ack == 10 &&
                   field(out, 322) == 1);
            wheel_sequence = 11;
            wheel_value = 0;
            assert(dataParser_makedumydata(out, 650) == 650 && qinggan11_wheel_ack == 11 &&
                   field(out, 322) == 0);
            wheel_sequence = 12;
            wheel_index = 332;
            wheel_value = 1;
            assert(dataParser_makedumydata(out, 650) == 650 && qinggan11_wheel_ack == 11);
            normal[310] = normal[358] = normal[104] = 0;
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
               field(cached, 310) == 0);

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
                invalid["fields"][0]["unsupported_initial"] = 0;
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
        auto legacy = source;
        legacy.erase("native_initial_fields");
        assert(voyemu::qnx_qinggan11_mcu_layout(legacy).find("qinggan11_calibration_fixture=0U") !=
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

        for (unsigned mutation = 0; mutation < 5; ++mutation) {
            auto invalid = source;

            if (mutation == 0) {
                invalid["native_initial_fields"]["eps_calibrationFlag"]["width"] = 1;
            }

            if (mutation == 1) {
                invalid["native_initial_fields"]["eps_calibrationFlag"]["start_bit"] = 464;
            }

            if (mutation == 2) {
                invalid["native_initial_fields"]["eps_calibrationFlag"]["index"] = 184;
            }

            if (mutation == 3) {
                invalid["hardware_adapter"] = "sa8155-qinggan09";
            }

            if (mutation == 4) {
                invalid.erase("native_initial_fields");
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
                   qinggan11_ack == 2 && field(frame, 310) == normal[310]);
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

        // Fields that reach the OEM ADAS, APA/PDC and power manager items or that no parser case of
        // this firmware reads; start bits and widths from the OEM 7.3.1 parser descriptors.
        {
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

            for (const Case &signal : std::initializer_list<Case>{
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
                 }) {
                const auto at = std::find_if(catalog.begin(), catalog.end(), [&](const auto &f) {
                    return f.name == signal.name;
                });
                assert(at != catalog.end());
                const auto position = static_cast<unsigned>(at - catalog.begin());
                const auto previous = normal[position];
                normal[position] = signal.value;
                reset();
                unsigned char frame[650];
                assert(dataParser_makedumydata(frame, 650) == 650 && qinggan11_ack == offered &&
                       packed(frame, signal.bit, signal.width) == signal.value);
                normal[position] = previous;
            }
        }

        // AdasEcu acc and cancel write acc_mode, mpc_icaSts and time_gap_set, which reach their OEM
        // ADAS items, and mrr_vSetDis, which this layout does not project. The guest rejects the
        // logical revision as a whole, and the host state for the guest carries the initial
        // mrr_vSetDis beside the new speed and the ADAS states.
        {
            auto state = voyemu::apply_patch(voyemu::initial_state(),
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
                   raw.at("mrr_vSetDis") == 160 && raw.at("time_gap_set") == 1);
            auto deliver = [&](const Json &values) {
                for (unsigned i = 0; i < 579; i++) {
                    normal[i] = values.at(catalog[i].name).get<unsigned>();
                }

                reset();
                unsigned char frame[650];
                const bool accepted = dataParser_makedumydata(frame, 650) == 650 &&
                                      qinggan11_ack == offered;

                return accepted && field(frame, 310) == 60;
            };
            assert(!deliver(raw) && qinggan11_rejects == 1);
            const auto guest = voyemu::qnx_qinggan11_guest_state(source, raw);
            assert(guest.at("acc_mode") == 0 && guest.at("mpc_icaSts") == 1 &&
                   guest.at("mrr_vSetDis") == 0 && guest.at("time_gap_set") == 1 &&
                   guest.at("ivi_languageSet") == raw.at("ivi_languageSet"));
            assert(deliver(guest) && !qinggan11_rejects);

            for (unsigned i = 0; i < 579; i++) {
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
