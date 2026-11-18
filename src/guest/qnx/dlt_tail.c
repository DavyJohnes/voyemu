/* Read only the existing OEM DLT file, with a fixed path and bounded extent. */
extern int open(const char *, int, ...), close(int);
extern long read(int, void *, usize), lseek(int, long, int);

static void relay_dlt_tail(void) {
    static long previous = -1;
    static unsigned samples;
    const char *flag = getenv("QGFX_DLT_TAIL");

    if (!flag || flag[0] != '1' || flag[1] || samples >= 1200) {
        return;
    }

    int fd = open("/log/dlt-ascii.log", 0);

    if (fd < 0) {
        return;
    }

    long end = lseek(fd, 0, 2);

    if (end <= 0 || end > 32 * 1024 * 1024 || end == previous) {
        close(fd);

        return;
    }

    long begin = end > 3072 ? end - 3072 : 0;
    unsigned char bytes[3072];

    if (lseek(fd, begin, 0) != begin) {
        close(fd);

        return;
    }

    long got = read(fd, bytes, (usize)(end - begin));
    close(fd);

    if (got != end - begin) {
        return;
    }

    acquire();
    u64 args[] = {(u64)begin, (u64)end};
    u64 ok = observation_call(357, args, 2, bytes, (unsigned)got);
    release();

    if (ok) {
        previous = end;
        samples++;
    }
}
