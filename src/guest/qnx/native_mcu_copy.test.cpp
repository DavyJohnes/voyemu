#include <atomic>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <future>
#include <mutex>
#include <thread>
#include <vector>
using u32 = unsigned;
using u64 = unsigned long long;
using usize = unsigned long;
struct TestPacket {
    u32 fault = 0, out_len = 0;
    unsigned char data[3840]{};
};
static TestPacket test_packet;
static TestPacket *packet = &test_packet;
static std::mutex api_mutex, remote_mutex;
static std::condition_variable remote_cv;
static bool enabled = true, async_mode = true, helper = false, fail_create = false,
            block_host = false, entered = false, release_host = false;
static unsigned wheel_malformed = 0, malformed = 0, host_ign = 0, native_calls = 0,
                create_calls = 0, join_calls = 0, dispatches = 0;
static std::atomic<unsigned> dlopen_calls{0}, dlsym_calls{0};
static std::atomic<unsigned> serializer_calls{0};
static std::thread *worker = nullptr;
static std::vector<u64> mcu_args, wheel_args;

static void copy(void *d, const void *s, usize n) {
    std::memcpy(d, s, n);
}

static void acquire() {
    api_mutex.lock();
}

static void release() {
    api_mutex.unlock();
}

static char *test_getenv(const char *name) {
    if (!std::strcmp(name, "QGFX_NATIVE_MCU")) {
        return enabled ? (char *)"1" : nullptr;
    }

    if (!std::strcmp(name, "QGFX_NATIVE_MCU_ASYNC")) {
        return async_mode ? (char *)"1" : nullptr;
    }

    return nullptr;
}

static int original_parser(void *out, unsigned short size) {
    native_calls++;

    if (out && size >= 280) {
        std::memset(out, 0x5a, 280);
    }

    return size >= 280 ? 280 : -1;
}

static int original_serializer(void *, void *out, unsigned size) {
    serializer_calls++;

    if (size < 40) {
        return -1;
    }

    auto *p = (unsigned char *)out;
    std::memset(p, 0, 40);
    unsigned parity = 0xc5;

    for (unsigned i = 0; i < 9; i++) {
        p[30 + i] = (unsigned char)i;
        parity ^= i;
    }

    p[39] = (unsigned char)parity;

    return 0;
}

static void *test_dlopen(const char *, int) {
    dlopen_calls++;

    return (void *)1;
}

static void *test_dlsym(void *, const char *name) {
    dlsym_calls++;

    return !std::strcmp(name, "dataParser_makedumydata") ? (void *)original_parser
                                                         : (void *)original_serializer;
}

static int test_usleep(unsigned us) {
    std::this_thread::sleep_for(std::chrono::microseconds(us));

    return 0;
}

static int test_pthread_create(int *id, const void *, void *(*entry)(void *), void *arg) {
    create_calls++;

    if (fail_create) {
        return 11;
    }

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
    join_calls++;

    return 0;
}

static u64 call(u32, const u64 *, unsigned, const void *, unsigned);
#include "mcu_catalog.inc"
#define getenv test_getenv
#define dlopen test_dlopen
#define dlsym test_dlsym
#define usleep test_usleep
#define pthread_create test_pthread_create
#define pthread_join test_pthread_join
#define QGFX_INITIALIZER
#define QGFX_NATIVE_MCU_DESTRUCTOR
#define QGFX_HELPER_DISPATCH()                                                                     \
    do {                                                                                           \
        ++dispatches;                                                                              \
        if (helper)                                                                                \
            return;                                                                                \
    } while (0)
#include "native_mcu_copy.c"
#include <exception>
#include <iostream>
#undef getenv
#undef dlopen
#undef dlsym
#undef usleep
#undef pthread_create
#undef pthread_join

static u64 call(u32 op, const u64 *args, unsigned n, const void *, unsigned) {
    assert(n == 1);
    std::unique_lock lock(remote_mutex);

    if (op == 364) {
        mcu_args.push_back(args[0]);
        entered = true;
        remote_cv.notify_all();

        if (block_host) {
            remote_cv.wait(lock, [] {
                return release_host;
            });
        }
    } else {
        assert(op == 363);
        wheel_args.push_back(args[0]);
    }

    packet->fault = 0;
    packet->out_len = 0;

    if (op == 364) {
        if (args[0] >= 2) {
            return 0;
        }

        u32 values[581]{};
        values[0] = 1;
        values[1] = 579;
        values[312] = 7;
        values[360] = 70;
        values[314] = 52;
        values[2] = host_ign;

        if (malformed == 1) {
            values[0] = 2;
        }

        if (malformed == 2) {
            values[314] = 101;
        }

        if (malformed == 3) {
            values[2] = 2;
        }

        if (malformed == 4) {
            values[323] = 1;
        }

        copy(packet->data, values, sizeof values);
        packet->out_len = malformed == 5 ? sizeof values - 1 : sizeof values;
        packet->fault = malformed == 6;

        return 2;
    }

    if (args[0] >= 12) {
        return 0;
    }

    u32 wheel[2] = {wheel_malformed == 1 ? 321U : 322U,
                    wheel_malformed == 2 ? 2U : (args[0] >= 11 ? 0U : 1U)};
    copy(packet->data, wheel, sizeof wheel);
    packet->out_len = wheel_malformed == 3 ? sizeof wheel - 1 : sizeof wheel;
    packet->fault = wheel_malformed == 4;

    return args[0] >= 11 ? 12 : 11;
}

static void wait_until(bool (*predicate)()) {
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);

    while (!predicate()) {
        assert(std::chrono::steady_clock::now() < deadline);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

static void reset() {
    assert(!worker);
    enabled = async_mode = true;
    helper = fail_create = block_host = entered = release_host = false;
    wheel_malformed = malformed = host_ign = 0;
    native_calls = create_calls = join_calls = dispatches = 0;
    mcu_args.clear();
    wheel_args.clear();
    serializer_calls = 0;
    test_packet = {};
    native_async_lock = native_async_stop = native_async_created = 0;
    native_async_mcu_staged = native_async_wheel_staged = 0;
    native_mcu_ack = native_wheel_ack = 0;
    native_mcu_have = native_mcu_settings_have = 0;
    std::memset(native_mcu_values, 0, sizeof native_mcu_values);
    std::memset(native_mcu_wheel, 0, sizeof native_mcu_wheel);
}

static bool staged() {
    if (!native_cache_try()) {
        return false;
    }

    bool yes = native_async_mcu_staged == 2;
    native_cache_release();

    return yes;
}

static unsigned field(const unsigned char *out, unsigned index) {
    unsigned result = 0;

    for (unsigned b = 0; b < native_mcu_width[index]; b++) {
        result |= ((out[(native_mcu_bit[index] + b) / 8] >> ((native_mcu_bit[index] + b) % 8)) & 1U)
                  << b;
    }

    return result;
}

int main() try {
    reset();
    async_mode = false;
    native_async_start();
    assert(!create_calls);
    unsigned char out[280];
    assert(dataParser_makedumydata(out, 280) == 280 && native_calls == 1 && native_mcu_ack == 2 &&
           native_wheel_ack == 11);
    reset();
    enabled = false;
    native_async_start();
    assert(!create_calls);
    assert(dataParser_makedumydata(out, 280) == 280 && mcu_args.empty());

    for (auto b : out) {
        assert(b == 0x5a);
    }

    reset();
    helper = true;
    native_async_start();
    assert(dispatches == 1 && !create_calls && mcu_args.empty());
    reset();
    fail_create = true;
    native_async_start();
    assert(create_calls == 1 && !native_async_created);
    assert(dataParser_makedumydata(out, 280) == 280 && mcu_args.empty() && !native_mcu_ack);

    reset();
    native_async_lock = 1;
    unsigned char once_settings[40];
    auto once = std::async(std::launch::async, [&] {
        return dataparser_makesoc2mcudata((void *)1, once_settings, 40);
    });
    wait_until([] {
        return serializer_calls.load() == 1;
    });
    assert(once.wait_for(std::chrono::milliseconds(30)) == std::future_status::timeout);
    native_cache_release();
    assert(once.get() == 0 && serializer_calls.load() == 1 && native_mcu_settings_have);
    assert(dataParser_makedumydata(out, 280) == 280 && !native_mcu_ack && !native_wheel_ack &&
           !native_mcu_have);

    for (unsigned i = 0; i < 10; i++) {
        assert(out[250 + i] == once_settings[30 + i]);
    }

    assert(mcu_args.empty() && wheel_args.empty());

    reset();
    block_host = true;
    native_async_start();
    {
        std::unique_lock lock(remote_mutex);
        assert(remote_cv.wait_for(lock, std::chrono::seconds(3), [] {
            return entered;
        }));
    }
    auto consume = std::async(std::launch::async, [&] {
        return dataParser_makedumydata(out, 280);
    });
    assert(consume.wait_for(std::chrono::milliseconds(500)) == std::future_status::ready);
    assert(consume.get() == 280 && native_calls == 1);

    for (auto b : out) {
        assert(b == 0x5a);
    }

    assert(!native_mcu_ack);
    unsigned char settings[40];
    assert(dataparser_makesoc2mcudata((void *)1, settings, 40) == 0);
    assert(native_mcu_settings_have);
    {
        std::lock_guard lock(remote_mutex);
        release_host = true;
    }
    remote_cv.notify_all();
    wait_until(staged);
    assert(native_mcu_ack == 0);
    assert(dataParser_makedumydata(out, 280) == 280);
    assert(native_mcu_ack == 2 && field(out, 310) == 7 && field(out, 312) == 52);
    native_async_shutdown();
    assert(join_calls == 1 && !worker);

    reset();
    native_async_poll();
    assert(native_async_mcu_staged == 2 && !native_mcu_ack && wheel_args.empty());
    native_async_poll();
    assert(mcu_args.size() == 1);
    assert(dataParser_makedumydata(out, 280) == 280 && native_mcu_ack == 2);
    native_async_poll();
    assert(native_async_wheel_staged == 11 && !native_wheel_ack && native_mcu_wheel[0] == 1);
    const auto wheel_count = wheel_args.size();
    native_async_poll();
    assert(wheel_args.size() == wheel_count && native_mcu_wheel[0] == 1);
    assert(dataParser_makedumydata(out, 280) == 280 && native_wheel_ack == 11 &&
           field(out, 321) == 1);
    native_async_poll();
    assert(native_async_wheel_staged == 12 && native_wheel_ack == 11 && native_mcu_wheel[0] == 0);
    assert(dataParser_makedumydata(out, 280) == 280 && native_wheel_ack == 12 &&
           field(out, 321) == 0);

    for (unsigned bad = 1; bad <= 6; bad++) {
        reset();
        malformed = bad;
        native_async_poll();
        assert(!native_async_mcu_staged && !native_mcu_have && !native_mcu_ack);
        assert(dataParser_makedumydata(out, 280) == 280);

        for (auto b : out) {
            assert(b == 0x5a);
        }
    }

    for (unsigned bad = 1; bad <= 4; bad++) {
        reset();
        native_async_poll();
        assert(dataParser_makedumydata(out, 280) == 280 && native_mcu_ack == 2);
        wheel_malformed = bad;
        native_async_poll();
        assert(!native_async_wheel_staged && !native_wheel_ack);
        assert(dataParser_makedumydata(out, 280) == 280 && !native_wheel_ack &&
               field(out, 321) == 0);
        native_async_poll();
        assert(wheel_args.back() == 0);
    }

    // A frame is never packed without the staged values: the consumer waits for the cache owner,
    // which only copies bounded memory, instead of sending the zeros of the original frame.
    reset();
    native_async_poll();
    native_async_lock = 1;
    auto packed = std::async(std::launch::async, [&] {
        return dataParser_makedumydata(out, 280);
    });
    assert(packed.wait_for(std::chrono::milliseconds(30)) == std::future_status::timeout);
    native_cache_release();
    assert(packed.get() == 280 && native_mcu_ack == 2 && field(out, 310) == 7 &&
           field(out, 312) == 52);

    // The originals are resolved once per process: a per-frame dlopen leaves a loader handle
    // behind that every later dlsym walks, so the OEM threads would slow down without bound.
    reset();
    unsigned char frame_settings[40];
    assert(dataParser_makedumydata(out, 280) == 280 &&
           dataparser_makesoc2mcudata((void *)1, frame_settings, 40) == 0);
    const unsigned opened = dlopen_calls.load(), resolved = dlsym_calls.load();

    for (unsigned frame = 0; frame < 1000; frame++) {
        assert(dataParser_makedumydata(out, 280) == 280 &&
               dataparser_makesoc2mcudata((void *)1, frame_settings, 40) == 0);
    }

    assert(opened <= 1 && resolved <= 2 && dlopen_calls.load() == opened &&
           dlsym_calls.load() == resolved);

    reset();
    block_host = true;
    native_async_start();
    {
        std::unique_lock lock(remote_mutex);
        assert(remote_cv.wait_for(lock, std::chrono::seconds(3), [] {
            return entered;
        }));
    }
    auto stop = std::async(std::launch::async, [] {
        native_async_shutdown();
    });
    wait_until([] {
        return __atomic_load_n(&native_async_stop, __ATOMIC_ACQUIRE) != 0;
    });
    {
        std::lock_guard lock(remote_mutex);
        release_host = true;
    }
    remote_cv.notify_all();
    assert(stop.wait_for(std::chrono::seconds(3)) == std::future_status::ready);
    stop.get();
    assert(!worker && join_calls == 1 && !native_async_mcu_staged && !native_mcu_ack);
    const auto count = mcu_args.size();
    native_async_poll();
    assert(mcu_args.size() == count);

    // The adapter forwards the model ignition into the OEM frame: off during the cold start of
    // the cluster, on after the startup sequence, over the OEM value of the dummy frame.
    for (const unsigned ignition : {0U, 1U, 0U}) {
        reset();
        async_mode = false;
        host_ign = ignition;
        assert(dataParser_makedumydata(out, 280) == 280 && native_mcu_ack == 2 &&
               field(out, 0) == ignition && field(out, 310) == 7);
    }

    std::puts("Native MCU opt-in mocks passed; no QNX frame, ABI/runtime or root-cause acceptance");
} catch (const std::exception &error) {
    std::cerr << error.what() << '\n';

    return 1;
}
