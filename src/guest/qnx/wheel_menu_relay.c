/* Deliver physical down/up phases through the original simulator only. */
static u64 wheel_acknowledged;
static unsigned wheel_delivered;
#include "passion_wheel_relay.c"

static int wheel_write(const char *text) {
    usize n = 0;

    while (text[n]) {
        n++;
    }

    void *file = mcu_file_open("/log/qgfx-mcu-input", "ab");

    if (!file) {
        return 0;
    }

    usize written = mcu_file_write(text, 1, n, file);
    int rc = mcu_file_close(file);

    if (written != n || rc) {
        mcu_failed = 1;
        emit("QG3 MCU input-stream-failed\n");

        return 0;
    }

    return 1;
}

static void relay_wheel_menu(void) {
    const char *flag = getenv("QGFX_WHEEL_RELAY");

    if (!flag || flag[0] != '1' || flag[1] || mcu_failed) {
        return;
    }

    u32 command[2];
    u64 args[] = {wheel_acknowledged};
    acquire();
    u64 seq = call(363, args, 1, 0, 0);
    unsigned valid = seq && seq > wheel_acknowledged && seq < 0x100000000ULL && packet &&
                     packet->out_len == sizeof(command) && !packet->fault;

    if (valid) {
        copy(command, packet->data, sizeof(command));
    }

    release();

    if (!valid || command[0] < 322 || command[0] > 331 || command[1] > 1) {
        return;
    }

    char text[48];
    int bytes = snprintf(text, sizeof(text), "1\n%u\n%u\n2\n", command[0], command[1]);
    const char *passion = getenv("QGFX_PASSION_WHEEL_RELAY");

    if (passion && passion[0] == '1' && !passion[1]) {
        if (!passion_wheel_publish(command[0], command[1])) {
            mcu_failed = 1;
            emit("QG3 Passion wheel publication failed; phase unacknowledged\n");

            return;
        }
    } else if (bytes <= 0 || (unsigned)bytes >= sizeof(text) || !wheel_write(text)) {
        return;
    }

    wheel_inputs[command[0] - 322] = command[1];
    wheel_acknowledged = seq;
    wheel_delivered++;
}
