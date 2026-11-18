// Narrow white-box tests compile the production host translation unit in place.
// This file is never linked into the voyemu executable.
#include "adb_host.cpp"

bool adb_host_test_event_replacement(AdbHost *h) {
    auto old = event(ADB_EVENT_DEVICE);

    old->serial = "emulator-2048";
    old->event.device_generation = 1;
    old->event.device_state = ADB_DEVICE_REMOVED;

    if (!emit(h, std::move(old))) {
        return false;
    }

    auto next = event(ADB_EVENT_DEVICE);
    next->serial = "emulator-2048";
    next->model.assign(h->max_output + 1, 'm');
    next->event.device_generation = 2;
    next->event.device_state = kCsDevice;

    if (emit(h, std::move(next))) {
        return false;
    }

    std::lock_guard lock(h->mutex);

    return h->events.size() == 1 && h->events.front()->event.device_generation == 1 &&
           h->events.front()->event.device_state == ADB_DEVICE_REMOVED && h->queued_output == 0 &&
           h->removal_events == 1 && h->queued_removals == 13;
}

bool adb_host_test_pending_device_retry(AdbHost *h) {
    auto e = event(ADB_EVENT_DEVICE);

    e->serial = "emulator-2048";
    e->event.device_generation = 1;
    e->event.device_state = ADB_DEVICE_REMOVED;

    if (!emit(h, std::move(e))) {
        return false;
    }

    h->retry_devices = true;

    return true;
}

bool adb_host_test_control_overflow(AdbHost *h, uint64_t id) {
    auto op = find_operation(h, id);

    if (!op || !op->opened || op->service == ADB_SERVICE_SHELL_V2_RAW) {
        return false;
    }

    auto *s = find_local_socket(op->local_id, 0);

    if (!s) {
        return false;
    }

    // Deterministic pressure at the exact post-transport/pre-reader queue boundary.
    // The peer cannot drain this queue during this looper callback.
    s->packet_queue.append(Block(4 * 1024 * 1024));

    return s->enqueue(s, Block(1)) < 0;
}

#ifndef _WIN32
bool adb_host_test_bound_stream_socket(AdbHost *h, uint64_t id) {
    auto op = find_operation(h, id);

    if (!op || !op->opened) {
        return false;
    }

    auto *socket = find_local_socket(op->local_id, 0);
    const int bytes = 4096;

    return socket &&
           adb_setsockopt(socket->fd, SOL_SOCKET, SO_SNDBUF, &bytes, sizeof(bytes)) == 0 &&
           adb_setsockopt(op->fd.get(), SOL_SOCKET, SO_RCVBUF, &bytes, sizeof(bytes)) == 0;
}

size_t adb_host_test_queued_stream_input(AdbHost *h, uint64_t id) {
    auto op = find_operation(h, id);
    auto *socket = op ? find_local_socket(op->local_id, 0) : nullptr;

    return socket ? socket->packet_queue.size() : 0;
}
#endif
