// Embedded ADB host: one owned emulator transport per process over numeric IPv4 loopback.
#pragma once

#include <cstddef>
#include <cstdint>

struct AdbHost;
using AdbOperationId = std::uint64_t;

struct AdbBytes {
    const std::uint8_t *data;
    std::size_t size;
};

enum {
    ADB_STATUS_OK = 0,
    ADB_STATUS_INVALID = -1,
    ADB_STATUS_UNSUPPORTED = -2,
    ADB_STATUS_OFFLINE = -3,
    ADB_STATUS_UNAUTHORIZED = -4,
    ADB_STATUS_BUSY = -5,
    ADB_STATUS_TIMEOUT = -6,
    ADB_STATUS_CANCELLED = -7,
    ADB_STATUS_PROTOCOL_ERROR = -8,
    ADB_STATUS_IO_ERROR = -9,
    ADB_STATUS_TRUNCATED = -10,
    ADB_STATUS_WOULD_BLOCK = -11,
    ADB_STATUS_STALE_DEVICE = -12
};

enum {
    ADB_SERVICE_SHELL_V2_RAW = 1,
    ADB_SERVICE_ROOT = 2,
    ADB_SERVICE_REMOUNT = 3,
    ADB_SERVICE_REBOOT = 4,
    ADB_SERVICE_EXEC_RAW = 5,
    ADB_SERVICE_SYNC = 6,
    ADB_SERVICE_OBSERVER = 7
};

enum {
    ADB_EVENT_HOST_READY = 1,
    ADB_EVENT_DEVICE = 2,
    ADB_EVENT_OPENED = 3,
    ADB_EVENT_STDOUT = 4,
    ADB_EVENT_STDERR = 5,
    ADB_EVENT_TERMINAL = 6
};

// Zero selects the default bound.
struct AdbHostConfig {
    std::uint64_t maximum_queued_input_bytes;
    std::uint64_t maximum_queued_output_bytes;
};

/* DEVICE state UINT32_MAX means this serial/generation was removed.
 * Remaining DEVICE states are upstream ConnectionState values 0..11. */
constexpr std::uint32_t ADB_DEVICE_REMOVED = UINT32_MAX;
constexpr std::uint32_t ADB_DEVICE_ONLINE = 7;

struct AdbEvent {
    uint32_t type;
    AdbOperationId operation;
    uint64_t device_generation;
    int32_t status;
    int32_t remote_exit_code;
    uint32_t has_remote_exit_code;
    uint32_t device_state;
    AdbBytes serial;
    AdbBytes model;
    AdbBytes product;
    AdbBytes payload;
    /* All views remain valid until release; release uses the library allocator. */
    void *private_event;
};

/* Register once before host creation. Strings are borrowed for this call only;
 * severity is android-base VERBOSE=0 through FATAL=6. The callback must not
 * unwind or call back into ADB. It may run concurrently on transport threads. */
using AdbLogCallback = void (*)(int32_t severity, const char *file, uint32_t line,
                                const char *message);
void adb_host_set_log_callback(AdbLogCallback callback);

/* One host create attempt per process; no restart after shutdown. */
int32_t adb_host_create(const AdbHostConfig *config, AdbHost **out_host);
int32_t adb_host_scan(AdbHost *host);
/* Retain one immutable emulator target. Only canonical even
 * console ports 1024..65534 are accepted; raw ADB is numeric 127.0.0.1:(N+1).
 * Repeating the same target is idempotent; replacing it is INVALID. Attachment
 * and bounded reconnect are asynchronous and emit the ordinary DEVICE events.
 * No hostname, arbitrary port, scanner, server, or fallback is exposed.
 */
int32_t adb_host_attach_emulator(AdbHost *host, uint32_t console_port);
/* The fence runs before and after TCP connect. It must not call back into the host.
 * Its context must remain live until detach or successful host shutdown. */
using AdbEmulatorFence = int32_t (*)(void *context);
int32_t adb_host_attach_owned_emulator(AdbHost *host, uint32_t console_port, AdbEmulatorFence fence,
                                       void *context);
/* Stop reconnect and close the retained transport. The port remains immutable. */
int32_t adb_host_detach_emulator(AdbHost *host);

/* Inputs are copied. generation prevents stale operations after transport reconnect.
 * SHELL_V2_RAW accepts command bytes and reports shell exit status.
 * SHELL_V2_RAW alone accepts a zero timeout for an explicitly retained stream.
 * ROOT/REMOUNT/REBOOT require empty command and open exactly root:/remount:/reboot:.
 * Their raw response is STDOUT, never shell framing; terminal has no exit code.
 * OK means the accepted stream ended, NOT successful root/remount/reboot: callers
 * must inspect service output and independently reconnect/verify target state.
 * EOF before OPENED is PROTOCOL_ERROR. Read errors are IO_ERROR. Accepted
 * ROOT/REBOOT EOF permits expected daemon restart/disconnect; REMOUNT requires
 * a clean stream close while transport is online, otherwise IO_ERROR. No arbitrary service names or
 * reboot modes are exposed. EXEC_RAW accepts command bytes, opens exec:, and has no exit code. SYNC
 * requires an empty command and opens sync:. OBSERVER accepts the 32 lowercase hex character
 * observer ID and opens localabstract:voyemu-ui-<id>. SYNC/OBSERVER support unframed writes. Only
 * SHELL_V2_RAW supports close_stdin. Other services reject writes. Raw data services require a
 * clean online close. Event/operation admission is bounded; a stalled consumer eventually gets
 * BUSY. Completed operations are reclaimed; later calls with retired IDs are INVALID.
 */
int32_t adb_host_open(AdbHost *host, AdbBytes serial, uint64_t generation, uint32_t service,
                      AdbBytes command, uint64_t timeout_ms, AdbOperationId *out_operation);
int32_t adb_host_write(AdbHost *host, AdbOperationId operation, AdbBytes bytes,
                       size_t *accepted_bytes);
int32_t adb_host_close_stdin(AdbHost *host, AdbOperationId operation);
/* SYNC starts with a 64 KiB output window. Return credit after consuming output. */
int32_t adb_host_grant_sync_output(AdbHost *host, AdbOperationId operation, size_t consumed);
/* Closes one stream; never kicks unrelated streams on the transport. */
int32_t adb_host_cancel(AdbHost *host, AdbOperationId operation);

/* One event consumer; timeout bounds waiting and does not cancel operations. */
int32_t adb_host_next_event(AdbHost *host, uint64_t wait_timeout_ms, AdbEvent *out_event);
void adb_host_event_release(AdbEvent *event);

/* Bounded wait; ADB_STATUS_TIMEOUT leaves cleanup running. Call shutdown again before destroy. */
int32_t adb_host_shutdown(AdbHost *host, uint64_t timeout_ms);
/* Requires successful shutdown and release of every outstanding event. */
void adb_host_destroy(AdbHost *host);
