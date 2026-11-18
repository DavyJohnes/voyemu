/* Transport numeric stdin to the original simulator; never set UI properties. */
#ifndef QGFX_MCU_TEST
extern void *mcu_file_open(const char *, const char *) __asm__("fopen");
extern usize mcu_file_write(const void *, usize, usize, void *) __asm__("fwrite");
extern int mcu_file_close(void *) __asm__("fclose");
#endif
static u32 wheel_inputs[10];
static u64 mcu_acknowledged;
static unsigned mcu_delivered, mcu_failed;
static u32 mcu_previous[579];
static unsigned mcu_have_snapshot;
#include "mcu_catalog.inc"

static void relay_mcu_input(void) {
    const char *flag = getenv("QGFX_MCU_RELAY");

    if (!flag || flag[0] != '1' || flag[1] || mcu_failed) {
        return;
    }

    u32 values[581];
    u64 args[] = {mcu_acknowledged};
    acquire();
    u64 seq = call(364, args, 1, 0, 0);
    unsigned valid = seq && seq > mcu_acknowledged && seq < 0x100000000ULL && packet &&
                     packet->out_len == sizeof(values) && !packet->fault;

    if (valid) {
        copy(values, packet->data, sizeof(values));
    }

    release();

    if (!valid || values[0] != 1 || values[1] != 579) {
        return;
    }

    for (unsigned i = 0; i < 579; i++) {
        if (values[i + 2] > mcu_field_max[i] || (mcu_transient[i] && values[i + 2])) {
            return;
        }
    }

    if (values[312] > 255 || values[360] != values[312] * 10 || values[314] > 100) {
        return;
    }

    char text[16384];
    unsigned bytes = 0;

    /* The original simulator retains fields until menu2 publishes the complete MCU structure. */
    for (unsigned i = 0; i < 579; i++) {
        /* Transient wheel phases have their own writer; a vehicle revision must not replay them. */
        if (mcu_have_snapshot && values[i + 2] == mcu_previous[i]) {
            continue;
        }

        int n = snprintf(text + bytes, sizeof(text) - bytes, "1\n%u\n%u\n", i + 1,
                         (i >= 321 && i <= 330) ? wheel_inputs[i - 321] : values[i + 2]);

        if (n <= 0 || (unsigned)n >= sizeof(text) - bytes) {
            return;
        }

        bytes += (unsigned)n;
    }

    if (!bytes) {
        mcu_acknowledged = seq;

        return;
    }

    if (sizeof(text) - bytes < 3) {
        return;
    }

    text[bytes++] = '2';
    text[bytes++] = '\n';
    text[bytes] = 0;
    void *file = mcu_file_open("/log/qgfx-mcu-input", "ab");

    if (!file) {
        return;
    }

    usize written = mcu_file_write(text, 1, bytes, file);
    int closed = mcu_file_close(file);

    if (written != (usize)bytes || closed) {
        /* A partial stream cannot safely be replayed into the CLI parser. */
        mcu_failed = 1;
        emit("QG3 MCU input-stream-failed\n");

        return;
    }

    copy(mcu_previous, values + 2, sizeof(mcu_previous));
    mcu_have_snapshot = 1;
    mcu_acknowledged = seq;
    mcu_delivered++;
}
