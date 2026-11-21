// AOSP sysdeps.h must precede the other ADB headers.
#include "sysdeps.h"

#include "adb_host.hpp"
#include "adb_io.h"
#include "adb_loopback.hpp"
#include "adb_loopback_socket.hpp"
#include "shell_protocol.h"
#include "transport.h"
#include <algorithm>
#include <android-base/logging.h>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <csignal>
#include <deque>
#include <future>
#include <map>
#include <mutex>
#include <thread>
#include <vector>
using namespace std::chrono_literals;
int adb_trace_mask = 0;
extern void adb_host_fdevent_dispose();
struct Operation;
struct EventStorage {
    AdbEvent event{};
    std::string serial, model, product;
    std::vector<uint8_t> bytes;
    size_t accounted = 0;
    bool reserved_removal = false;
};
struct AdbHost {
    std::mutex mutex;
    std::condition_variable events_cv;
    std::deque<std::unique_ptr<EventStorage>> events;
    std::map<uint64_t, std::shared_ptr<Operation>> operations;
    std::thread worker, watchdog, shutdown_worker, connector;
    std::mutex emulator_lifecycle;
    std::mutex connector_mutex;
    std::condition_variable connector_cv;
    std::atomic<uint32_t> emulator_console_port{0};
    std::atomic<bool> emulator_paused{true};
    AdbEmulatorFence emulator_fence = nullptr;
    void *emulator_fence_context = nullptr;
    std::mutex shutdown_mutex;
    std::condition_variable shutdown_cv;
    bool cleanup_started = false, cleanup_complete = false;
    std::atomic<bool> stopping{false}, stopped{false};
    std::atomic<size_t> outstanding{0};
    size_t queued_output = 0, queued_removals = 0, removal_events = 0, max_input = 262144,
           max_output = 4194304;
    std::atomic<bool> retry_devices{false};
    uint64_t next_id = 1;
};
struct Operation {
    AdbHost *host;
    uint64_t id, generation;
    unique_fd fd;
    unsigned local_id = 0;
    std::atomic<bool> opened{false}, done{false}, finished{false}, launched{false},
            closed_online{false};
    uint32_t service = ADB_SERVICE_SHELL_V2_RAW;
    std::atomic<int> requested_status{0};
    std::chrono::steady_clock::time_point deadline;
    std::mutex input_mutex;
    std::condition_variable input_cv;
    std::deque<std::vector<uint8_t>> input;
    size_t queued_input = 0;
    bool close_stdin = false;
    std::mutex sync_mutex;
    std::condition_variable sync_cv;
    size_t sync_credit = 65536;
    std::thread reader, writer;
    void (*original_ready)(asocket *) = nullptr;
    void (*original_close)(asocket *) = nullptr;
};
static std::atomic<AdbHost *> active_host{nullptr};
static std::atomic<bool> ever_created{false};
static std::map<uint64_t, std::string> known_devices;                  // looper only
static std::map<unsigned, std::weak_ptr<Operation>> socket_operations; // looper only

static std::string copy_bytes(AdbBytes b) {
    return b.size ? std::string(reinterpret_cast<const char *>(b.data), b.size) : std::string();
}

static bool valid_bytes(AdbBytes b, size_t max) {
    return (!b.size || b.data) && b.size <= max;
}

static void sync_loop(std::function<void()> fn) {
    auto promise = std::make_shared<std::promise<void>>();
    auto future = promise->get_future();

    fdevent_run_on_looper([fn = std::move(fn), promise]() {
        fn();
        promise->set_value();
    });
    future.get();
}

static void release_event_budget(AdbHost *h, const EventStorage *e) {
    if (e->reserved_removal) {
        h->queued_removals -= e->accounted;
        --h->removal_events;
    } else {
        h->queued_output -= e->accounted;
    }
}

static bool emit(AdbHost *h, std::unique_ptr<EventStorage> e) {
    size_t cost = 0;

    for (size_t size : {e->bytes.size(), e->serial.size(), e->model.size(), e->product.size()}) {
        if (size > SIZE_MAX - cost) {
            return false;
        }

        cost += size;
    }

    const bool removed = e->event.type == ADB_EVENT_DEVICE &&
                         e->event.device_state == ADB_DEVICE_REMOVED;
    std::lock_guard lock(h->mutex);
    // Refund a same-serial replacement for admission, but retain the truthful old
    // state until the replacement has actually been admitted.
    size_t refund_output = 0, refund_removals = 0, refund_count = 0, refund_removal_count = 0;
    const auto replaces = [&](const auto &old) {
        return e->event.type == ADB_EVENT_DEVICE && old->event.type == ADB_EVENT_DEVICE &&
               old->serial == e->serial;
    };

    for (const auto &old : h->events) {
        if (replaces(old)) {
            ++refund_count;

            if (old->reserved_removal) {
                refund_removals += old->accounted;
                ++refund_removal_count;
            } else {
                refund_output += old->accounted;
            }
        }
    }

    const auto output = h->queued_output - refund_output;
    const auto removals = h->queued_removals - refund_removals;
    const auto removal_count = h->removal_events - refund_removal_count;

    if (removed) {
        // Charged control reserve remains deliverable when ordinary output is full.
        if (cost > 1024 || removal_count >= 4096 || cost > 4194304 - removals) {
            return false;
        }
    } else if (h->events.size() - refund_count - removal_count >= 4096 ||
               cost > h->max_output - output) {
        return false;
    }

    for (auto it = h->events.begin(); it != h->events.end();) {
        if (replaces(*it)) {
            release_event_budget(h, it->get());
            it = h->events.erase(it);
        } else {
            ++it;
        }
    }

    if (removed) {
        h->queued_removals += cost;
        ++h->removal_events;
        e->reserved_removal = true;
    } else {
        h->queued_output += cost;
    }

    e->accounted = cost;
    h->events.push_back(std::move(e));
    h->events_cv.notify_one();

    return true;
}

static std::unique_ptr<EventStorage> event(uint32_t type, Operation *op = nullptr) {
    auto e = std::make_unique<EventStorage>();

    e->event.type = type;

    if (op) {
        e->event.operation = op->id;
        e->event.device_generation = op->generation;
    }

    return e;
}

static bool selected_transport(AdbHost *h, const atransport *t) {
    const auto port = h->emulator_console_port.load();

    return port ? !h->emulator_paused && !t->kicked() && t->type == kTransportLocal &&
                           t->serial == "emulator-" + std::to_string(port)
                : t->type == kTransportUsb;
}

void adb_host_transport_changed() {
    auto *h = active_host.load();

    if (!h || h->stopping) {
        return;
    }

    std::map<uint64_t, std::string> current, next_known;
    std::vector<const atransport *> selected;
    std::vector<uint64_t> kicks;
    iterate_transports([&](const atransport *t) {
        if (!selected_transport(h, t)) {
            return true;
        }

        if (t->serial.empty() || t->serial.size() > 1024 || selected.size() >= 4096) {
            kicks.push_back(t->id);

            return true;
        }

        current[t->id] = t->serial;
        selected.push_back(t);

        return true;
    });

    // Old removals precede new online states for the same serial.
    for (const auto &[generation, serial] : known_devices) {
        if (current.count(generation)) {
            next_known[generation] = serial;
            continue;
        }

        auto e = event(ADB_EVENT_DEVICE);
        e->event.device_generation = generation;
        e->event.device_state = ADB_DEVICE_REMOVED;
        e->serial = serial;

        if (!emit(h, std::move(e))) {
            next_known[generation] = serial;
            h->retry_devices = true;
        }
    }

    for (const auto *t : selected) {
        if (!next_known.count(t->id) && next_known.size() >= 4096) {
            kicks.push_back(t->id);
            continue;
        }

        auto e = event(ADB_EVENT_DEVICE);
        e->event.device_generation = t->id;
        e->event.device_state = t->GetConnectionState();
        e->serial = t->serial;
        e->model = t->model;
        e->product = t->product;

        if (emit(h, std::move(e))) {
            next_known[t->id] = t->serial;
        } else {
            kicks.push_back(t->id);
        }
    }

    known_devices = std::move(next_known);

    // Defer stopping until iteration is complete; retain IDs, never raw pointers.
    for (auto id : kicks) {
        fdevent_run_on_looper([id]() {
            atransport *selected = nullptr;
            iterate_transports([&](const atransport *t) {
                if (t->id == id) {
                    selected = const_cast<atransport *>(t);
                }

                return true;
            });

            if (selected) {
                selected->Kick();
            }
        });
    }
}

void adb_notify_device_scan_complete() {}

void update_transport_status() {}

void adb_wait_for_device_initialization() {}

static std::shared_ptr<Operation> find_operation(AdbHost *h, uint64_t id) {
    if (!h) {
        return {};
    }

    std::lock_guard lock(h->mutex);
    auto it = h->operations.find(id);

    return it == h->operations.end() ? nullptr : it->second;
}

static void close_core(const std::shared_ptr<Operation> &op) {
    if (!op->local_id) {
        return;
    }

    auto *s = find_local_socket(op->local_id, 0);

    if (s) {
        s->packet_queue.clear();
        s->close(s);
    }

    op->local_id = 0;
}

static void request_stop(const std::shared_ptr<Operation> &op, int status) {
    if (op->done) {
        return;
    }

    int expected = 0;
    op->requested_status.compare_exchange_strong(expected, status);
    adb_shutdown(op->fd.get());
    {
        std::lock_guard lock(op->input_mutex);
        op->input_cv.notify_all();
    }
    {
        std::lock_guard lock(op->sync_mutex);
        op->sync_cv.notify_all();
    }
}

void adb_host_socket_overflow(asocket *s) {
    auto it = socket_operations.find(s->id);
    auto op = it == socket_operations.end() ? nullptr : it->second.lock();

    if (op) {
        request_stop(op, ADB_STATUS_TRUNCATED);
    }
}

static void ready_hook(asocket *s) {
    auto it = socket_operations.find(s->id);
    auto op = it == socket_operations.end() ? nullptr : it->second.lock();

    if (!op) {
        return;
    }

    if (!op->opened.exchange(true)) {
        emit(op->host, event(ADB_EVENT_OPENED, op.get()));
    }

    op->original_ready(s);
}

static void close_hook(asocket *s) {
    auto it = socket_operations.find(s->id);
    auto op = it == socket_operations.end() ? nullptr : it->second.lock();

    socket_operations.erase(s->id);

    if (op) {
        op->closed_online = s->transport && s->transport->online;

        // A credit-starved SYNC reader may be waiting on its CV instead of the fd.
        if (op->service == ADB_SERVICE_SYNC && !op->closed_online) {
            request_stop(op, ADB_STATUS_IO_ERROR);
        }

        op->local_id = 0;
        s->ready = op->original_ready;
        s->close = op->original_close;
        s->close(s);
    }
}

static bool raw_duplex(uint32_t service) {
    return service == ADB_SERVICE_SYNC || service == ADB_SERVICE_OBSERVER;
}

static bool writable_service(uint32_t service) {
    return service == ADB_SERVICE_SHELL_V2_RAW || raw_duplex(service);
}

static void writer_main(const std::shared_ptr<Operation> &op) {
    auto codec = std::make_unique<ShellProtocol>(op->fd.get());

    for (;;) {
        std::vector<uint8_t> bytes;
        bool close = false;
        {
            std::unique_lock lock(op->input_mutex);
            op->input_cv.wait(lock, [&]() {
                return op->done || op->requested_status || !op->input.empty() || op->close_stdin;
            });

            if (op->done || op->requested_status) {
                return;
            }

            if (!op->input.empty()) {
                bytes = std::move(op->input.front());
                op->input.pop_front();
                op->queued_input -= bytes.size();
            } else {
                close = op->close_stdin;
            }
        }

        if (raw_duplex(op->service)) {
            if (!WriteFdExactly(op->fd.get(), bytes.data(), bytes.size())) {
                request_stop(op, ADB_STATUS_IO_ERROR);

                return;
            }

            continue;
        }

        if (close) {
            codec->Write(ShellProtocol::kIdCloseStdin, 0);

            return;
        }

        for (size_t offset = 0; offset < bytes.size();) {
            size_t n = std::min(bytes.size() - offset, codec->data_capacity());
            memcpy(codec->data(), bytes.data() + offset, n);

            if (!codec->Write(ShellProtocol::kIdStdin, n)) {
                return;
            }

            offset += n;
        }
    }
}

static void reader_main(const std::shared_ptr<Operation> &op) {
    auto codec = std::make_unique<ShellProtocol>(op->fd.get());
    bool has_exit = false, reboot_reply = false;
    int exit_code = 0, status = ADB_STATUS_PROTOCOL_ERROR;

    if (op->service != ADB_SERVICE_SHELL_V2_RAW) {
        uint8_t buffer[4096];

        for (;;) {
            size_t capacity = sizeof(buffer);

            if (op->service == ADB_SERVICE_SYNC) {
                std::unique_lock lock(op->sync_mutex);
                op->sync_cv.wait(lock, [&]() {
                    return op->requested_status || op->sync_credit;
                });

                if (op->requested_status) {
                    break;
                }

                capacity = std::min(capacity, op->sync_credit);
            }

            auto n = adb_read(op->fd.get(), buffer, capacity);

            if (n < 0 && errno == EINTR) {
                continue;
            }

            if (n < 0) {
                status = ADB_STATUS_IO_ERROR;
                break;
            }

            if (n == 0) {
                status = !op->opened ? ADB_STATUS_PROTOCOL_ERROR
                                     : (op->service != ADB_SERVICE_ROOT &&
                                                        op->service != ADB_SERVICE_REBOOT &&
                                                        !op->closed_online
                                                ? ADB_STATUS_IO_ERROR
                                                : ADB_STATUS_OK);
                break;
            }

            if (op->service == ADB_SERVICE_SYNC) {
                std::lock_guard lock(op->sync_mutex);
                op->sync_credit -= n;
            }

            if (op->service == ADB_SERVICE_REBOOT) {
                reboot_reply = true;
            }

            auto e = event(ADB_EVENT_STDOUT, op.get());
            e->bytes.assign(buffer, buffer + n);

            if (!emit(op->host, std::move(e))) {
                status = ADB_STATUS_TRUNCATED;
                break;
            }
        }
    } else {
        while (codec->Read()) {
            int id = codec->id();
            size_t n = codec->data_length();

            if (id == ShellProtocol::kIdExit) {
                if (n != 1) {
                    break;
                }

                has_exit = true;
                exit_code = static_cast<uint8_t>(codec->data()[0]);
                status = ADB_STATUS_OK;
                break;
            }

            if (id != ShellProtocol::kIdStdout && id != ShellProtocol::kIdStderr) {
                break;
            }

            auto e = event(id == ShellProtocol::kIdStdout ? ADB_EVENT_STDOUT : ADB_EVENT_STDERR,
                           op.get());
            e->bytes.assign(codec->data(), codec->data() + n);

            if (!emit(op->host, std::move(e))) {
                status = ADB_STATUS_TRUNCATED;
                break;
            }
        }
    }

    if (op->requested_status) {
        status = op->requested_status;
    }

    op->done = true;
    adb_shutdown(op->fd.get());
    {
        std::lock_guard lock(op->input_mutex);
        op->input_cv.notify_all();
    }

    if (op->writer.joinable()) {
        op->writer.join();
    }

    sync_loop([&]() {
        close_core(op);
        auto *host = op->host;

        if (status != ADB_STATUS_OK || op->service != ADB_SERVICE_REBOOT || reboot_reply ||
            host->stopping || !host->emulator_console_port) {
            return;
        }

        // The default reboot service is silent on success. The emulator can
        // close the service while leaving its old TCP transport connected.
        atransport *retired = nullptr;
        iterate_transports([&](const atransport *transport) {
            if (transport->id == op->generation && selected_transport(host, transport)) {
                retired = const_cast<atransport *>(transport);
            }

            return retired == nullptr;
        });

        if (retired) {
            retired->Kick();
        }
    });
    auto e = event(ADB_EVENT_TERMINAL, op.get());
    e->event.status = status;
    e->event.remote_exit_code = exit_code;
    e->event.has_remote_exit_code = has_exit;
    // A terminal event must remain deliverable even when the byte budget is exhausted.
    {
        std::lock_guard lock(op->host->mutex);
        op->host->events.push_back(std::move(e));
        op->host->events_cv.notify_one();
    }
    op->finished = true;
}

void adb_host_set_log_callback(AdbLogCallback callback) {
    android::base::SetLogger([callback](android::base::LogId, android::base::LogSeverity severity,
                                        const char *, const char *file, unsigned int line,
                                        const char *message) {
        if (callback) {
            callback(static_cast<int32_t>(severity), file, line, message);
        }
    });
}

int32_t adb_host_create(const AdbHostConfig *cfg, AdbHost **out) {
    if (!cfg || !out || cfg->maximum_queued_input_bytes > 16 * 1024 * 1024 ||
        cfg->maximum_queued_output_bytes > 64 * 1024 * 1024) {
        return ADB_STATUS_INVALID;
    }

    *out = nullptr;

    if (ever_created.exchange(true)) {
        return ADB_STATUS_BUSY;
    }

    adb_loopback_ignore_broken_pipe();
    auto *h = new AdbHost();

    if (cfg->maximum_queued_input_bytes) {
        h->max_input = cfg->maximum_queued_input_bytes;
    }

    if (cfg->maximum_queued_output_bytes) {
        h->max_output = cfg->maximum_queued_output_bytes;
    }

    active_host = h;
    h->worker = std::thread([h]() {
        emit(h, event(ADB_EVENT_HOST_READY));
        fdevent_loop();
        adb_host_fdevent_dispose();
        h->stopped = true;
        h->events_cv.notify_all();
    });
    h->watchdog = std::thread([h]() {
        while (!h->stopping) {
            std::vector<std::shared_ptr<Operation>> ops;
            {
                std::lock_guard lock(h->mutex);

                for (auto &[id, op] : h->operations) {
                    ops.push_back(op);
                }
            }

            for (auto &op : ops) {
                if (!op->done && std::chrono::steady_clock::now() >= op->deadline) {
                    request_stop(op, ADB_STATUS_TIMEOUT);
                }

                if (op->finished && op->launched) {
                    // Reap on the watchdog, never on the stream's own reader thread.
                    if (op->reader.joinable()) {
                        op->reader.join();
                    }

                    std::lock_guard lock(h->mutex);
                    h->operations.erase(op->id);
                }
            }

            std::this_thread::sleep_for(10ms);
        }
    });
    *out = h;

    return ADB_STATUS_OK;
}

static void emulator_main(AdbHost *h) {
    const auto port = h->emulator_console_port.load();
    const auto serial = "emulator-" + std::to_string(port);
    const auto active = [&] {
        return !h->stopping && !h->emulator_paused;
    };
    const auto owned = [&] {
        return active() && (!h->emulator_fence || h->emulator_fence(h->emulator_fence_context));
    };

    while (active()) {
        bool attached = false;
        sync_loop([&]() {
            iterate_transports([&](const atransport *t) {
                if (selected_transport(h, t)) {
                    attached = true;
                }

                return true;
            });
        });

        if (!attached && owned()) {
            auto connection = adb_host_connect_emulator(port, h->emulator_paused);

            if (connection && owned()) {
                auto adapter = std::make_shared<BlockingConnectionAdapter>(std::move(connection));
                sync_loop([&]() {
                    if (active()) {
                        adb_host_register_emulator(adapter, serial);
                    }
                });
            }
        }

        std::unique_lock lock(h->connector_mutex);
        h->connector_cv.wait_for(lock, 250ms, [&]() {
            return !active();
        });
    }
}

static int32_t attach_emulator(AdbHost *h, uint32_t console_port, AdbEmulatorFence fence,
                               void *context) {
    if (!h || console_port < 1024 || console_port > 65534 || console_port % 2) {
        return ADB_STATUS_INVALID;
    }

    std::lock_guard lifetime(h->emulator_lifecycle);
    std::lock_guard lock(h->shutdown_mutex);

    if (h->stopping) {
        return ADB_STATUS_INVALID;
    }

    const auto selected = h->emulator_console_port.load();

    if (selected && selected != console_port) {
        return ADB_STATUS_INVALID;
    }

    if (!h->emulator_paused) {
        return h->emulator_fence == fence && h->emulator_fence_context == context
                       ? ADB_STATUS_OK
                       : ADB_STATUS_INVALID;
    }

    h->emulator_console_port = console_port;
    h->emulator_fence = fence;
    h->emulator_fence_context = context;
    h->emulator_paused = false;
    try {
        h->connector = std::thread(emulator_main, h);
    } catch (...) {
        h->emulator_paused = true;

        return ADB_STATUS_IO_ERROR;
    }

    return ADB_STATUS_OK;
}

int32_t adb_host_attach_emulator(AdbHost *h, uint32_t console_port) {
    return attach_emulator(h, console_port, nullptr, nullptr);
}

int32_t adb_host_attach_owned_emulator(AdbHost *h, uint32_t console_port, AdbEmulatorFence fence,
                                       void *context) {
    return fence ? attach_emulator(h, console_port, fence, context) : ADB_STATUS_INVALID;
}

int32_t adb_host_detach_emulator(AdbHost *h) {
    if (!h) {
        return ADB_STATUS_INVALID;
    }

    std::lock_guard lifetime(h->emulator_lifecycle);

    if (h->stopping) {
        return ADB_STATUS_INVALID;
    }

    if (h->emulator_paused.exchange(true)) {
        return ADB_STATUS_OK;
    }

    h->connector_cv.notify_all();

    if (h->connector.joinable()) {
        h->connector.join();
    }

    h->emulator_fence = nullptr;
    h->emulator_fence_context = nullptr;
    sync_loop([]() {
        kick_all_transports();
        adb_host_transport_changed();
    });

    return ADB_STATUS_OK;
}

int32_t adb_host_scan(AdbHost *h) {
    if (!h || h->stopping) {
        return ADB_STATUS_INVALID;
    }

    sync_loop([]() {
        adb_host_transport_changed();
    });

    return ADB_STATUS_OK;
}

int32_t adb_host_open(AdbHost *h, AdbBytes serial, uint64_t generation, uint32_t service,
                      AdbBytes command, uint64_t timeout, AdbOperationId *out) {
    constexpr size_t maximum_open_payload = 128 * 1024;

    if (!h || h->stopping || !out || !valid_bytes(serial, 1024) || !serial.size ||
        !valid_bytes(command, maximum_open_payload) ||
        (!timeout && service != ADB_SERVICE_SHELL_V2_RAW) || timeout > 3600000) {
        return ADB_STATUS_INVALID;
    }

    if (service < ADB_SERVICE_SHELL_V2_RAW || service > ADB_SERVICE_OBSERVER) {
        return ADB_STATUS_UNSUPPORTED;
    }

    if (service != ADB_SERVICE_SHELL_V2_RAW && service != ADB_SERVICE_EXEC_RAW &&
        service != ADB_SERVICE_OBSERVER && command.size) {
        return ADB_STATUS_INVALID;
    }

    auto serial_s = copy_bytes(serial), command_s = copy_bytes(command);

    if (serial_s.find('\0') != std::string::npos || command_s.find('\0') != std::string::npos) {
        return ADB_STATUS_INVALID;
    }

    if (service == ADB_SERVICE_OBSERVER &&
        (command_s.size() != 32 ||
         command_s.find_first_not_of("0123456789abcdef") != std::string::npos)) {
        return ADB_STATUS_INVALID;
    }

    if (service == ADB_SERVICE_EXEC_RAW && command_s.empty()) {
        return ADB_STATUS_INVALID;
    }

    std::string destination;

    switch (service) {
    case ADB_SERVICE_SHELL_V2_RAW:
        destination = "shell,v2,raw:" + command_s;
        break;
    case ADB_SERVICE_ROOT:
        destination = "root:";
        break;
    case ADB_SERVICE_REMOUNT:
        destination = "remount:";
        break;
    case ADB_SERVICE_REBOOT:
        destination = "reboot:";
        break;
    case ADB_SERVICE_EXEC_RAW:
        destination = "exec:" + command_s;
        break;
    case ADB_SERVICE_SYNC:
        destination = "sync:";
        break;
    case ADB_SERVICE_OBSERVER:
        destination = "localabstract:voyemu-ui-" + command_s;
        break;
    default:
        return ADB_STATUS_UNSUPPORTED;
    }

    // The service prefix and terminating NUL count toward the host payload limit.
    if (destination.size() >= maximum_open_payload) {
        return ADB_STATUS_INVALID;
    }

    auto op = std::make_shared<Operation>();
    op->host = h;
    op->generation = generation;
    op->service = service;
    op->deadline = timeout ? std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout)
                           : std::chrono::steady_clock::time_point::max();
    {
        std::lock_guard lock(h->mutex);

        if (h->operations.size() >= 128 || h->events.size() + h->operations.size() >= 4096) {
            return ADB_STATUS_BUSY;
        }

        op->id = h->next_id++;
    }
    int result = ADB_STATUS_OFFLINE;
    sync_loop([&]() {
        atransport *selected = nullptr;
        iterate_transports([&](const atransport *t) {
            if (selected_transport(h, t) && t->serial == serial_s) {
                selected = const_cast<atransport *>(t);
            }

            return true;
        });

        if (!selected) {
            return;
        }

        if (selected->id != generation) {
            result = ADB_STATUS_STALE_DEVICE;

            return;
        }

        if (selected->GetConnectionState() == kCsUnauthorized ||
            selected->GetConnectionState() == kCsAuthorizing) {
            result = ADB_STATUS_UNAUTHORIZED;

            return;
        }

        if (!selected->online) {
            return;
        }

        if (service == ADB_SERVICE_SHELL_V2_RAW && !selected->has_feature(kFeatureShell2)) {
            result = ADB_STATUS_UNSUPPORTED;

            return;
        }

        if (destination.size() + 1 > selected->get_max_payload()) {
            result = ADB_STATUS_INVALID;

            return;
        }

        int fds[2];

        if (adb_socketpair(fds) < 0) {
            result = ADB_STATUS_IO_ERROR;

            return;
        }

        unique_fd local(fds[0]);
        op->fd.reset(fds[1]);
#ifdef SO_NOSIGPIPE
        int no_sigpipe = 1;

        if (adb_setsockopt(local.get(), SOL_SOCKET, SO_NOSIGPIPE, &no_sigpipe, sizeof(no_sigpipe)) <
                    0 ||
            adb_setsockopt(op->fd.get(), SOL_SOCKET, SO_NOSIGPIPE, &no_sigpipe,
                           sizeof(no_sigpipe)) < 0) {
            result = ADB_STATUS_IO_ERROR;

            return;
        }
#endif
        auto *s = create_local_socket(std::move(local));
        s->transport = selected;
        op->local_id = s->id;
        op->original_ready = s->ready;
        op->original_close = s->close;
        s->ready = ready_hook;
        s->close = close_hook;
        socket_operations[s->id] = op;
        connect_to_remote(s, destination);
        result = ADB_STATUS_OK;
    });

    if (result != ADB_STATUS_OK) {
        return result;
    }

    {
        std::lock_guard lock(h->mutex);
        h->operations.emplace(op->id, op);
    }

    if (writable_service(service)) {
        op->writer = std::thread(writer_main, op);
    }

    op->reader = std::thread(reader_main, op);
    op->launched = true;
    *out = op->id;

    return ADB_STATUS_OK;
}

int32_t adb_host_write(AdbHost *h, AdbOperationId id, AdbBytes b, size_t *accepted) {
    if (!accepted || !valid_bytes(b, 16 * 1024 * 1024)) {
        return ADB_STATUS_INVALID;
    }

    *accepted = 0;
    auto op = find_operation(h, id);

    if (!op) {
        return ADB_STATUS_INVALID;
    }

    if (!writable_service(op->service)) {
        return ADB_STATUS_UNSUPPORTED;
    }

    std::lock_guard lock(op->input_mutex);

    if (op->done || op->requested_status || op->close_stdin) {
        return ADB_STATUS_INVALID;
    }

    size_t n = std::min(b.size, h->max_input - op->queued_input);

    if (!n && b.size) {
        return ADB_STATUS_WOULD_BLOCK;
    }

    if (n) {
        op->input.emplace_back(b.data, b.data + n);
        op->queued_input += n;
    }

    *accepted = n;
    op->input_cv.notify_one();

    return ADB_STATUS_OK;
}

int32_t adb_host_grant_sync_output(AdbHost *h, AdbOperationId id, size_t consumed) {
    auto op = find_operation(h, id);

    if (!op) {
        return ADB_STATUS_INVALID;
    }

    if (op->service != ADB_SERVICE_SYNC) {
        return ADB_STATUS_UNSUPPORTED;
    }

    std::lock_guard lock(op->sync_mutex);

    if (consumed > 65536 - op->sync_credit) {
        return ADB_STATUS_INVALID;
    }

    op->sync_credit += consumed;
    op->sync_cv.notify_one();

    return ADB_STATUS_OK;
}

int32_t adb_host_close_stdin(AdbHost *h, AdbOperationId id) {
    auto op = find_operation(h, id);

    if (!op) {
        return ADB_STATUS_INVALID;
    }

    if (op->service != ADB_SERVICE_SHELL_V2_RAW) {
        return ADB_STATUS_UNSUPPORTED;
    }

    std::lock_guard lock(op->input_mutex);

    if (op->done) {
        return ADB_STATUS_INVALID;
    }

    op->close_stdin = true;
    op->input_cv.notify_one();

    return ADB_STATUS_OK;
}

int32_t adb_host_cancel(AdbHost *h, AdbOperationId id) {
    auto op = find_operation(h, id);

    if (!op) {
        return ADB_STATUS_INVALID;
    }

    request_stop(op, ADB_STATUS_CANCELLED);

    return ADB_STATUS_OK;
}

int32_t adb_host_next_event(AdbHost *h, uint64_t wait, AdbEvent *out) {
    if (!h || !out || wait > 3600000) {
        return ADB_STATUS_INVALID;
    }

    std::unique_lock lock(h->mutex);

    if (!h->events_cv.wait_for(lock, std::chrono::milliseconds(wait), [&]() {
            return !h->events.empty() || h->stopped.load();
        })) {
        return ADB_STATUS_TIMEOUT;
    }

    if (h->events.empty()) {
        return ADB_STATUS_OFFLINE;
    }

    auto e = h->events.front().release();
    h->events.pop_front();
    release_event_budget(h, e);
    // Serialize admission with stopping/disposal; queued events remain drainable
    // after the looper has gone away.
    {
        std::lock_guard shutdown_lock(h->shutdown_mutex);

        if (!h->stopping && h->retry_devices.exchange(false)) {
            fdevent_run_on_looper([]() {
                adb_host_transport_changed();
            });
        }
    }
    e->event.serial = {reinterpret_cast<const uint8_t *>(e->serial.data()), e->serial.size()};
    e->event.model = {reinterpret_cast<const uint8_t *>(e->model.data()), e->model.size()};
    e->event.product = {reinterpret_cast<const uint8_t *>(e->product.data()), e->product.size()};
    e->event.payload = {e->bytes.data(), e->bytes.size()};
    e->event.private_event = e;
    *out = e->event;
    h->outstanding++;

    return ADB_STATUS_OK;
}

void adb_host_event_release(AdbEvent *e) {
    if (!e || !e->private_event) {
        return;
    }

    delete static_cast<EventStorage *>(e->private_event);
    e->private_event = nullptr;

    if (auto h = active_host.load()) {
        h->outstanding--;
    }
}

static void shutdown_main(AdbHost *h) {
    {
        std::lock_guard lifetime(h->emulator_lifecycle);
        h->emulator_paused = true;
        h->connector_cv.notify_all();

        if (h->connector.joinable()) {
            h->connector.join();
        }
    }

    if (h->watchdog.joinable()) {
        h->watchdog.join();
    }

    std::vector<std::shared_ptr<Operation>> ops;
    {
        std::lock_guard lock(h->mutex);

        for (auto &[id, op] : h->operations) {
            ops.push_back(op);
        }
    }

    for (auto &op : ops) {
        request_stop(op, ADB_STATUS_CANCELLED);
    }

    for (auto &op : ops) {
        if (op->reader.joinable()) {
            op->reader.join();
        }
    }

    sync_loop([&]() {
        kick_all_transports();
    });
    sync_loop([]() {});
    sync_loop([]() {});
    sync_loop([]() {
        fdevent_terminate_loop();
    });
    h->worker.join();
    {
        std::lock_guard lock(h->shutdown_mutex);
        h->cleanup_complete = true;
    }
    h->shutdown_cv.notify_all();
}

int32_t adb_host_shutdown(AdbHost *h, uint64_t timeout) {
    if (!h || timeout > 3600000) {
        return ADB_STATUS_INVALID;
    }

    std::unique_lock lock(h->shutdown_mutex);

    if (!h->cleanup_started) {
        h->cleanup_started = true;
        h->stopping = true;
        h->shutdown_worker = std::thread(shutdown_main, h);
    }

    if (!h->shutdown_cv.wait_for(lock, std::chrono::milliseconds(timeout), [&]() {
            return h->cleanup_complete;
        })) {
        return ADB_STATUS_TIMEOUT;
    }

    if (h->shutdown_worker.joinable()) {
        h->shutdown_worker.join();
    }

    return ADB_STATUS_OK;
}

void adb_host_destroy(AdbHost *h) {
    if (!h || !h->cleanup_complete || h->shutdown_worker.joinable() || h->outstanding) {
        return;
    }

    active_host = nullptr;
    delete h;
}
