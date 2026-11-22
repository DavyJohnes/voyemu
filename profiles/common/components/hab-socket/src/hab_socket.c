/* Message channels of the Qualcomm HAB API as the OEM libmrpc uses them (habmm_socket_open, _send,
 * _recv and _close on MM_MISC 601), carried over one TCP connection per channel of the interguest
 * link instead of the hypervisor. One shared implementation for the cluster and the multimedia;
 * hab_os_* are the thin guest backends. A frame is a u32 big-endian size and the message. */
typedef unsigned char HabU8;
typedef unsigned int HabU32;
typedef unsigned long HabSize;

/* Connects or accepts the TCP channel of minor; returns the descriptor or a negative errno. */
int hab_os_open(HabU32 minor, int timeout_ms);
long hab_os_read(int fd, void *data, HabSize size);
long hab_os_write(int fd, const void *data, HabSize size);
/* 1 when fd is readable, 0 on timeout, negative errno on failure; -1 waits without limit. */
int hab_os_wait(int fd, int timeout_ms);
void hab_os_close(int fd);
void hab_os_log(const char *text);
int hab_os_timed_out(void);
int hab_os_interrupted(void);

#define HAB_MM_MISC 601U
#define HAB_HANDLE_BASE 0x56480000
#define HAB_CHANNELS 16
#define HAB_MESSAGE_MAX (8U * 1024U * 1024U)
#define HAB_EIO 5
#define HAB_ENOMEM 12
#define HAB_EINVAL 22

typedef struct {
    int fd; /* descriptor + 1; 0 marks a free slot */
    HabU32 mmid;
    int writing;
} HabChannel;

static HabChannel hab_channels[HAB_CHANNELS];

static void hab_log(const char *what, HabU32 mmid, int handle, int value) {
    static const char digits[] = "0123456789abcdef";
    char line[96];
    unsigned n = 0;

    for (const char *p = "QHB "; *p;) {
        line[n++] = *p++;
    }

    for (; *what && n < 40; ++what) {
        line[n++] = *what;
    }

    HabU32 fields[3] = {mmid, (HabU32)handle, (HabU32)value};
    const char *names[3] = {" mmid=0x", " handle=0x", " value=0x"};

    for (unsigned f = 0; f < 3; ++f) {
        for (const char *p = names[f]; *p;) {
            line[n++] = *p++;
        }

        for (int shift = 28; shift >= 0; shift -= 4) {
            line[n++] = digits[(fields[f] >> shift) & 15];
        }
    }

    line[n++] = '\n';
    line[n] = 0;
    hab_os_log(line);
}

/* True when handle names a channel of this implementation. */
int hab_socket_owns(int handle) {
    return handle >= HAB_HANDLE_BASE && handle < HAB_HANDLE_BASE + HAB_CHANNELS;
}

/* The OEM libmrpc opens MM_MISC with its channel number in the minor byte. */
int hab_socket_serves(HabU32 mmid) {
    return (mmid & 0xffffU) == HAB_MM_MISC;
}

static int hab_timeout(HabU32 timeout) {
    return timeout == 0xffffffffU || timeout > 0x7fffffffU ? -1 : (int)timeout;
}

int hab_socket_open(int *handle, HabU32 mmid, HabU32 timeout, HabU32 flags) {
    (void)flags;

    if (!handle || !hab_socket_serves(mmid)) {
        return -HAB_EINVAL;
    }

    int slot = -1;

    for (int i = 0; i < HAB_CHANNELS && slot < 0; ++i) {
        int free = 0;

        if (__atomic_compare_exchange_n(&hab_channels[i].fd, &free, -1, 0, __ATOMIC_ACQ_REL,
                                        __ATOMIC_RELAXED)) {
            slot = i;
        }
    }

    if (slot < 0) {
        hab_log("open no-slot", mmid, 0, 0);

        return -HAB_ENOMEM;
    }

    int fd = hab_os_open(mmid >> 16, hab_timeout(timeout));

    if (fd < 0) {
        __atomic_store_n(&hab_channels[slot].fd, 0, __ATOMIC_RELEASE);
        hab_log("open failed", mmid, 0, fd);

        return fd;
    }

    hab_channels[slot].mmid = mmid;
    hab_channels[slot].writing = 0;
    __atomic_store_n(&hab_channels[slot].fd, fd + 1, __ATOMIC_RELEASE);
    *handle = HAB_HANDLE_BASE + slot;
    hab_log("open", mmid, *handle, fd);

    return 0;
}

static HabChannel *hab_channel(int handle) {
    if (!hab_socket_owns(handle)) {
        return 0;
    }

    HabChannel *c = &hab_channels[handle - HAB_HANDLE_BASE];

    return __atomic_load_n(&c->fd, __ATOMIC_ACQUIRE) > 0 ? c : 0;
}

int hab_socket_close(int handle) {
    HabChannel *c = hab_channel(handle);

    if (!c) {
        return -HAB_EINVAL;
    }

    int fd = __atomic_exchange_n(&c->fd, -1, __ATOMIC_ACQ_REL) - 1;
    hab_os_close(fd);
    hab_log("close", c->mmid, handle, 0);
    __atomic_store_n(&c->fd, 0, __ATOMIC_RELEASE);

    return 0;
}

static int hab_write_all(int fd, const HabU8 *data, HabU32 size) {
    while (size) {
        long n = hab_os_write(fd, data, size);

        if (n < 0 && hab_os_interrupted()) {
            continue;
        }

        if (n <= 0) {
            return -HAB_EIO;
        }

        data += n;
        size -= (HabU32)n;
    }

    return 0;
}

static int hab_read_all(int fd, HabU8 *data, HabU32 size) {
    while (size) {
        long n = hab_os_read(fd, data, size);

        if (n < 0 && hab_os_interrupted()) {
            continue;
        }

        if (n <= 0) {
            return -HAB_EIO;
        }

        data += n;
        size -= (HabU32)n;
    }

    return 0;
}

int hab_socket_send(int handle, const void *data, HabU32 size, HabU32 flags) {
    HabChannel *c = hab_channel(handle);
    (void)flags;

    if (!c || !data || !size || size > HAB_MESSAGE_MAX) {
        return -HAB_EINVAL;
    }

    HabU8 head[4] = {(HabU8)(size >> 24), (HabU8)(size >> 16), (HabU8)(size >> 8), (HabU8)size};

    while (__atomic_exchange_n(&c->writing, 1, __ATOMIC_ACQUIRE)) {}

    int fd = __atomic_load_n(&c->fd, __ATOMIC_ACQUIRE) - 1;
    int rc = fd < 0 ? -HAB_EIO : hab_write_all(fd, head, 4);

    if (!rc) {
        rc = hab_write_all(fd, data, size);
    }

    __atomic_store_n(&c->writing, 0, __ATOMIC_RELEASE);

    if (rc) {
        hab_log("send failed", c->mmid, handle, rc);
    }

    return rc;
}

/* One whole message; a message longer than the buffer is consumed and refused. */
int hab_socket_recv(int handle, void *data, HabU32 *size, HabU32 timeout, HabU32 flags) {
    HabChannel *c = hab_channel(handle);
    (void)flags;

    if (!c || !data || !size || !*size) {
        return -HAB_EINVAL;
    }

    int fd = __atomic_load_n(&c->fd, __ATOMIC_ACQUIRE) - 1;
    int ready = hab_os_wait(fd, hab_timeout(timeout));

    if (ready == 0) {
        return hab_os_timed_out();
    }

    HabU8 head[4];

    if (ready < 0 || hab_read_all(fd, head, 4)) {
        hab_log("recv closed", c->mmid, handle, ready);

        return -HAB_EIO;
    }

    HabU32 length = (HabU32)head[0] << 24 | (HabU32)head[1] << 16 | (HabU32)head[2] << 8 | head[3];

    if (!length || length > HAB_MESSAGE_MAX) {
        hab_log("recv frame", c->mmid, handle, (int)length);

        return -HAB_EIO;
    }

    if (length > *size) {
        HabU8 drop[256];

        for (HabU32 left = length; left;) {
            HabU32 n = left < sizeof(drop) ? left : (HabU32)sizeof(drop);

            if (hab_read_all(fd, drop, n)) {
                return -HAB_EIO;
            }

            left -= n;
        }

        hab_log("recv oversize", c->mmid, handle, (int)length);
        *size = 0;

        return -HAB_EINVAL;
    }

    if (hab_read_all(fd, data, length)) {
        hab_log("recv closed", c->mmid, handle, 0);

        return -HAB_EIO;
    }

    *size = length;

    return 0;
}
