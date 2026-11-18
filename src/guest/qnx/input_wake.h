/* Optional capability; old hosts leave ACK zero and keep the original wait cadence. */
#define INPUT_WAKE_MAGIC 0x31454b41574e4951ULL
_Static_assert(__builtin_offsetof(Packet, reserved) == 176, "Input wake packet offsets");
static u64 input_wake_seen;

static void input_wake_advertise(void) {
    if (packet) {
        __atomic_store_n(&packet->reserved[0], INPUT_WAKE_MAGIC, __ATOMIC_RELEASE);
    }
}

static void input_wait(unsigned duration, const unsigned *stop) {
    if (!packet || __atomic_load_n(&packet->reserved[1], __ATOMIC_ACQUIRE) != INPUT_WAKE_MAGIC) {
        usleep(duration);

        return;
    }

    while (duration) {
        u64 value = __atomic_load_n(&packet->reserved[2], __ATOMIC_ACQUIRE);

        if (value && value != input_wake_seen) {
            input_wake_seen = value;

            return;
        }

        if (__atomic_load_n(stop, __ATOMIC_ACQUIRE)) {
            return;
        }

        unsigned slice = duration < 10000 ? duration : 10000;
        usleep(slice);
        duration -= slice;
    }
}
