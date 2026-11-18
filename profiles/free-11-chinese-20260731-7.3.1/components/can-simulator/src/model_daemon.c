/* Development MCU process: one model for raw transport and semantic clients. */
#define _GNU_SOURCE
#include "model_queue.h"
#include "model_protocol.h"
#include "listener_policy.h"
#include "model_fixture.h"
#include "source_lease.h"
#include "model_oracle.h"
#include "observer_protocol.h"
#include "boot_id.h"
#include "startup_guard.h"
#include "rx_schedule.h"
#ifdef __ANDROID__
#include <sys/system_properties.h>
#endif
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/random.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "boundary_daemon.h"

#define MAX_CLIENTS 8
#define CLIENT_UID 1000
#define DEADLINE_MS 500
#ifdef __ANDROID__
static void property_value(void *cookie, const char *name, const char *value, uint32_t serial)
{
    char *out = cookie;
    (void)name; (void)serial;
    snprintf(out, PROP_VALUE_MAX, "%s", value);
}
static int boot_property(char out[PROP_VALUE_MAX])
{
    char probe[PROP_VALUE_MAX] = {0};
    const prop_info *p = __system_property_find("sys.voyemu.early.probe");
    if (!p) return -1;
    __system_property_read_callback(p, property_value, probe);
    if (strcmp(probe, "1")) return -1;
    p = __system_property_find("sys.boot_completed");
    if (!p) return 1;
    __system_property_read_callback(p, property_value, out);
    return 0;
}
#endif

static int64_t now_ms(void)
{
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts)) return -1;
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static int load_fixture(struct mcu_model *model, uint8_t *drive, int32_t *initial_acc)
{
    char bytes[128];
    struct stat before, after;
    int fd = open("/system/etc/voyemu/can-model.fixture", O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    ssize_t n;
    if (fd < 0) return -1;
    if (fstat(fd, &before) || !S_ISREG(before.st_mode) || before.st_uid != 0 ||
        (before.st_mode & 022) || before.st_size <= 0 || before.st_size > (off_t)sizeof(bytes)) {
        close(fd); return -1;
    }
    n = read(fd, bytes, (size_t)before.st_size);
    if (n != before.st_size || fstat(fd, &after) || before.st_dev != after.st_dev ||
        before.st_ino != after.st_ino || before.st_size != after.st_size ||
        before.st_mtime != after.st_mtime || before.st_ctime != after.st_ctime) {
        close(fd); return -1;
    }
    close(fd);
    return model_fixture_parse(bytes, (size_t)n, model, drive, initial_acc) ? 0 : -1;
}

static uint64_t boot_ns(void)
{
    struct timespec ts;
    if (clock_gettime(CLOCK_BOOTTIME, &ts) || ts.tv_sec < 0 || ts.tv_nsec < 0) return 0;
    if ((uint64_t)ts.tv_sec > (UINT64_MAX - (uint64_t)ts.tv_nsec) / 1000000000ULL) return 0;
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

static int read_boot_id(uint8_t id[16])
{
    char text[37]; int fd = open("/proc/sys/kernel/random/boot_id", O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    ssize_t n;
    if (fd < 0) return -1;
    n = read(fd, text, sizeof(text)); close(fd);
    return n == sizeof(text) && parse_boot_id(text, sizeof(text), id) ? 0 : -1;
}

static int emit_frame(int fd, struct model_queue *m, struct model_oracle *o, const uint8_t frame[10])
{
    if (oracle_append_frame(o, m->epoch, boot_ns(), ORACLE_EMIT, frame) != ORACLE_OK) return -1;
    return write(fd, frame, 10) == 10 ? 0 : -1;
}

/* A kernel without long datagram support rejects them; the short frame stream continues. */
static int emit_service(int fd, struct model_queue *m, struct model_oracle *o, bool *supported)
{
    static uint8_t datagram[RX_SERVICE_DATAGRAM_SIZE];
    if (!*supported) return 0;
    rx_service_datagram(datagram);
    if (oracle_append_frame(o, m->epoch, boot_ns(), ORACLE_EMIT, datagram) != ORACLE_OK) return -1;
    if (write(fd, datagram, sizeof(datagram)) == (ssize_t)sizeof(datagram)) return 0;
    if (errno != EINVAL) return -1;
    *supported = false;
    return 0;
}

static int send_feedback(int fd, struct model_queue *m, struct model_oracle *o, int32_t state)
{
    uint8_t frame[10];
    bool known = state == VEHICLE_WARNING ? mcu_warning_feedback(&m->state, frame)
                                          : mcu_forced_feedback(&m->state, frame);
    return known ? emit_frame(fd, m, o, frame) : 0;
}

static int send_power(int fd, struct model_queue *m, struct model_oracle *o)
{
    uint8_t frame[10] = {0x6a, 8, 0, 0, 0, 0, 0, 0, 0, 0};
    frame[2] = m->acc == 2 ? 2 : 1;
    return emit_frame(fd, m, o, frame);
}

/* 1: retain source lease, -1: uncertain lease reply, 0: close client. */
static int request(int fd, struct model_queue *m, struct model_oracle *o, bool source_present)
{
    uint8_t wire[MODEL_REQUEST_SIZE], reply[MODEL_REPLY_SIZE];
    uint32_t op, status;
    ssize_t n = recv(fd, wire, sizeof(wire), MSG_TRUNC | MSG_DONTWAIT);
    if (n < 0 || !model_protocol(m, source_present, wire, (size_t)n, reply)) return 0;
    memcpy(&op, wire + 4, sizeof(op)); memcpy(&status, reply + 4, sizeof(status));
    if ((ntohl(op) == 2 || ntohl(op) == 3) && ntohl(status) == 0) {
        uint32_t state, value;
        memcpy(&state, wire+8, sizeof(state)); memcpy(&value, wire+12, sizeof(value));
        if (oracle_append(o, m->epoch, boot_ns(), ORACLE_ADMISSION,
                          (int32_t)ntohl(state), (int32_t)ntohl(value)) != ORACLE_OK) return -2;
    }
    if (send(fd, reply, sizeof(reply), MSG_NOSIGNAL) != sizeof(reply)) {
        if ((ntohl(op) == 2 || ntohl(op) == 3) && ntohl(status) == 0 &&
            oracle_append(o, m->epoch, boot_ns(), ORACLE_LOSS, 0, 2) != ORACLE_OK) return -2;
        return ntohl(op) == 4 && ntohl(status) == 0 ? -1 : 0;
    }
    return ntohl(op) == 4 && ntohl(status) == 0 ? 1 : 0;
}

static int inherited_fd(const char *name)
{
    const char *value = getenv(name); char *end; long fd;
    if (!value || !*value) return -1;
    errno = 0; fd = strtol(value, &end, 10);
    return errno || *end || fd < 0 || fd > 0x7fffffff ? -1 : (int)fd;
}

static void observer_request(int fd, int device, struct model_queue *model, const struct model_oracle *oracle)
{
    uint8_t wire[BOUNDARY_REQUEST_SIZE], reply[BOUNDARY_REPLY_SIZE];
    ssize_t n = recv(fd, wire, sizeof(wire), MSG_TRUNC | MSG_DONTWAIT);
    if (n == BOUNDARY_REQUEST_SIZE && boundary_word(wire,0)==BOUNDARY_WIRE_MAGIC) {
        struct boundary_request request;
        struct can_boundary_control control={0};
        int status=CAN_BOUNDARY_INVALID;
        if (!boundary_decode_request(wire,(size_t)n,&request))
            status=boundary_handle(device,model,&request,boot_ns(),&control);
        boundary_encode_reply(reply,status,&control);
        (void)send(fd,reply,BOUNDARY_REPLY_SIZE,MSG_NOSIGNAL);
    } else if (n >= 0 && observer_protocol(oracle, model, boot_ns(), wire, (size_t)n, reply))
        (void)send(fd, reply, OBSERVER_REPLY_SIZE, MSG_NOSIGNAL);
}

static int revoke_lease(struct pollfd *fds, int *lease, struct model_queue *model, struct model_oracle *oracle, int device)
{
    int i;
    for (i = 0; i < MAX_CLIENTS; ++i) {
        struct pollfd *p = &fds[3+i];
        if (p->fd != *lease) continue;
        close(p->fd); p->fd = -1; p->revents = 0; break;
    }
    *lease = -1;
    if (model->admission_closed) return oracle_append(oracle,model->epoch,boot_ns(),ORACLE_LOSS,0,-1)==ORACLE_OK?0:-1;
    if (boundary_model_lost(device,model)) return -1;
    if (oracle_append(oracle, model->epoch, boot_ns(), ORACLE_LOSS, 0, -1) != ORACLE_OK) return -1;
    return boundary_model_reset_queue(device,model);
}

int main(void)
{
    struct model_queue model;
    struct model_oracle oracle;
    struct mcu_model fixture;
    uint8_t boot_id[16];
    uint64_t session;
    struct pollfd fds[3 + MAX_CLIENTS];
    int64_t deadlines[MAX_CLIENTS] = {0};
    bool observer[MAX_CLIENTS] = {0};
    int listener, observation, device, i, lease = -1;
    uint8_t drive;
    int32_t initial_acc;
    int witness;
    int64_t next_drive = 0, next_service = 0;
    int32_t delivered_acc = -1;
    bool service_supported = true;
#ifdef __ANDROID__
    char ready[PROP_VALUE_MAX] = {0}, boot_before[PROP_VALUE_MAX] = {0}, boot_after[PROP_VALUE_MAX] = {0};
    int before_absent, after_absent;
    __system_property_get("sys.voyemu.early.ready", ready);
    before_absent = boot_property(boot_before);
    if (before_absent < 0) return 2;
    if (!startup_ready(ready)) return 2;
#else
    const char *boot_before = "", *boot_after = "";
    int before_absent = -1, after_absent = -1;
#endif
    if (read_boot_id(boot_id) || load_fixture(&fixture, &drive, &initial_acc) || getrandom(&session, sizeof(session), GRND_NONBLOCK) != sizeof(session) || !session) return 2;
    listener = inherited_fd("ANDROID_SOCKET_voyemu_can_model");
    observation = inherited_fd("ANDROID_SOCKET_voyemu_can_observe");
    if (listener < 0 || observation < 0 || listener == observation) return 2;
    device = open("/dev/cis_can_model", O_RDWR | O_CLOEXEC | O_NONBLOCK);
    if (device < 0) return 2;
    witness = ioctl(device, 3, 0);
    if (witness != 0 && witness != 1) return 2;
#ifdef __ANDROID__
    after_absent = boot_property(boot_after);
#endif
    model_init(&model, &fixture, drive, startup_fixture_allowed(witness, before_absent, boot_before, after_absent, boot_after) ? initial_acc : -1, session);
    if (boundary_model_init(device,&model)) return 2;
    oracle_init(&oracle, session, boot_id, witness);
    if (oracle_append(&oracle, model.epoch, boot_ns(), ORACLE_BOOT, 0, model.acc) != ORACLE_OK) return 2;
    if (listen(listener, MAX_CLIENTS) || listen(observation, MAX_CLIENTS) ||
        fcntl(listener, F_SETFL, O_NONBLOCK) || fcntl(observation, F_SETFL, O_NONBLOCK)) return 2;
    memset(fds, 0, sizeof(fds));
    fds[0].fd = device; fds[0].events = POLLIN;
    fds[1].fd = listener; fds[1].events = POLLIN;
    fds[2].fd = observation; fds[2].events = POLLIN;
    for (i = 0; i < MAX_CLIENTS; ++i) fds[3 + i].fd = -1;
    for (;;) {
        int n;
        int64_t now;
        int32_t changed;
        struct semantic_request pending = {0};
        int step;
        if (boundary_refresh(device,&model)) break;
        if (lease >= 0) {
            int broken = source_lease_broken(lease);
            if (broken < 0) break;
            if (broken) { if (revoke_lease(fds, &lease, &model, &oracle, device)) break; continue; }
        }
        /* Accepted semantic work precedes any later observation of raw TX. */
        if (model.count) pending = model.queue[model.head];
        step = model.admission_closed && !model.count ? 0 : boundary_model_step(device,&model,&changed);
        if (step == MODEL_FATAL) break;
        if (step == MODEL_STALE || step == MODEL_NOOP) {
            int kind = step == MODEL_STALE ? ORACLE_DROP : ORACLE_NOOP;
            if (oracle_append(&oracle, model.epoch, boot_ns(), kind, pending.state, pending.value) != ORACLE_OK) break;
            continue;
        }
        if (step == 1) {
            int value = changed == 0 ? model.acc : model_get(&model, changed);
            if (oracle_append(&oracle, model.epoch, boot_ns(), ORACLE_STATE, changed, value) != ORACLE_OK) break;
            if (changed == 0) {
                if (model.acc == 2 &&
                    (oracle_append(&oracle, model.epoch, boot_ns(), ORACLE_STATE, VEHICLE_WARNING,
                                   model_get(&model, VEHICLE_WARNING)) != ORACLE_OK ||
                     oracle_append(&oracle, model.epoch, boot_ns(), ORACLE_STATE, VEHICLE_FORCED_EV,
                                   model_get(&model, VEHICLE_FORCED_EV)) != ORACLE_OK)) break;
                if (oracle_append(&oracle, model.epoch, boot_ns(), ORACLE_LOSS, 0, 1) != ORACLE_OK) break;
                if (boundary_model_reset_queue(device,&model) || send_power(device, &model, &oracle)) break;
                if (model.acc == 2 && (send_feedback(device, &model, &oracle, VEHICLE_WARNING) ||
                                       send_feedback(device, &model, &oracle, VEHICLE_FORCED_EV))) break;
                delivered_acc = model.acc;
            } else if (send_feedback(device, &model, &oracle, changed)) break;
            continue;
        }
        n = poll(fds, 3 + MAX_CLIENTS, 100);
        now = now_ms();
        if (n < 0 && errno == EINTR) continue;
        if (n < 0 || now < 0 || listener_poll_fatal(fds[0].revents) ||
            listener_poll_fatal(fds[1].revents) || listener_poll_fatal(fds[2].revents)) break;
        if (lease >= 0) {
            int broken = source_lease_broken(lease);
            if (broken < 0) break;
            if (broken) { if (revoke_lease(fds, &lease, &model, &oracle, device)) break; continue; }
        }
        if (rx_acc_due(&delivered_acc, model.acc)) {
            if (send_power(device, &model, &oracle)) break;
            if (model.acc == 2 && (send_feedback(device, &model, &oracle, VEHICLE_WARNING) ||
                                   send_feedback(device, &model, &oracle, VEHICLE_FORCED_EV))) break;
        }
        if (now >= next_drive) {
            uint8_t frame[10] = {0x73, 8, 0, 0, 0, 0, 0, 0, 0, 0};
            frame[2] = model.drive - 1;
            if (model.acc == 2 && emit_frame(device, &model, &oracle, frame)) break;
            next_drive = now + 500;
        }
        if (now >= next_service) {
            if (model.acc == 2 && emit_service(device, &model, &oracle, &service_supported)) break;
            next_service = now + RX_SERVICE_PERIOD_MS;
        }
        if (fds[0].revents & POLLIN) {
            uint8_t frame[10];
            ssize_t got = read(device, frame, sizeof(frame));
            if (got == sizeof(frame)) {
                int applied;
                if (oracle_append_frame(&oracle, model.epoch, boot_ns(), ORACLE_RAW_READ, frame) != ORACLE_OK) break;
                applied = boundary_model_raw(device,&model,frame,&changed);
                if (applied == MODEL_FATAL) break;
                if (applied == 1 && (oracle_append(&oracle, model.epoch, boot_ns(), ORACLE_STATE,
                                                   changed, model_get(&model, changed)) != ORACLE_OK ||
                                     send_feedback(device, &model, &oracle, changed))) break;
            } else if (got < 0 && errno != EAGAIN && errno != EINTR) break;
            else if (got >= 0) break;
        }
        for (i = 1; i <= 2; ++i) {
            if (fds[i].revents & POLLIN) {
                int client = accept4(fds[i].fd, NULL, NULL, SOCK_CLOEXEC | SOCK_NONBLOCK);
                int slot;
                if (client < 0 && listener_accept_fatal(errno)) return 1;
                if (client < 0) continue;
                {
                    struct ucred cred; socklen_t len = sizeof(cred);
                    for (slot = 0; slot < MAX_CLIENTS && fds[3+slot].fd >= 0; ++slot) {}
                    if (slot == MAX_CLIENTS || getsockopt(client, SOL_SOCKET, SO_PEERCRED, &cred, &len) ||
                        len != sizeof(cred) || cred.uid != (i == 1 ? CLIENT_UID : 0)) close(client);
                    else {
                        fds[3+slot].fd = client; fds[3+slot].events = POLLIN;
                        observer[slot] = i == 2; deadlines[slot] = now + DEADLINE_MS;
                    }
                }
            }
        }
        for (i = 0; i < MAX_CLIENTS; ++i) {
            struct pollfd *p = &fds[3+i];
            int result = 0;
            if (p->fd < 0) continue;
            if (p->fd == lease) continue;
            if (p->revents & POLLIN) {
                if (observer[i]) observer_request(p->fd, device, &model, &oracle);
                else result = request(p->fd, &model, &oracle, lease >= 0);
            }
            if (result == -2) return 1;
            if (result == 1) { lease = p->fd; deadlines[i] = INT64_MAX; continue; }
            if (result == -1) {
                if (boundary_model_lost(device,&model) ||
                    oracle_append(&oracle, model.epoch, boot_ns(), ORACLE_LOSS, 0, -1) != ORACLE_OK ||
                    boundary_model_reset_queue(device,&model)) return 1;
            }
            if ((p->revents & (POLLIN | POLLERR | POLLHUP | POLLNVAL)) || now >= deadlines[i]) {
                close(p->fd); p->fd = -1; p->revents = 0;
            }
        }
    }
    close(device);
    return 1;
}
