#ifndef VOYEMU_QNX_BOUNDED_WAIT_H
#define VOYEMU_QNX_BOUNDED_WAIT_H
/* Dedicated initialized preload carrier only; no OEM startup/CRT replacement. */
typedef unsigned long long QbwTime;
enum {
    QBW_INTERNAL = 125,
    QBW_TIMEOUT = 124,
    QBW_SPAWN_FAILED = 127
};
#define QBW_DEADLINE_NS 40100000000ULL
#define QBW_TERM_GRACE_NS 100000000ULL
typedef struct {
    void *opaque;
    int (*now)(void *, QbwTime *);
    int (*owner)(void *);
    int (*spawn)(void *, int *);
    /* 1=reaped exit, 0=live, 2=EINTR, -1=ECHILD/lost ownership, -2=other error. */
    int (*poll)(void *, int, int *);
    int (*pause)(void *, QbwTime);
    int (*signal)(void *, int, int); /* 1=TERM, 2=KILL; only the direct child. */
    int (*reap)(void *, int);
} QbwOps;
typedef struct {
    int code, timed_out, spawned, internal_error, child_reaped, cleanup_unverified;
    unsigned polls;
} QbwResult;

static void qbw_cleanup(const QbwOps *o, int pid, QbwResult *r) {
    int killed = o->signal(o->opaque, pid, 2);

    if (killed < 0) {
        /* A failed KILL cannot justify a blocking wait on a possibly live child. */
        int code = 0;
        int state = o->poll(o->opaque, pid, &code);
        ++r->polls;
        r->internal_error = 1;
        r->child_reaped = state == 1;
        r->cleanup_unverified = state != 1;

        return;
    }

    int reaped = o->reap(o->opaque, pid);
    r->child_reaped = reaped == 0;

    if (reaped) {
        r->internal_error = 1;
        r->cleanup_unverified = 1;
    }
}

static QbwResult qbw_run(const QbwOps *o) {
    QbwResult r = {QBW_INTERNAL, 0, 0, 1, 0, 0, 0};
    QbwTime start = 0, now = 0, previous = 0, deadline = 0;
    int pid = 0, code = 0;

    if (!o || !o->now || !o->owner || !o->spawn || !o->poll || !o->pause || !o->signal ||
        !o->reap) {
        return r;
    }

    if (!o->owner(o->opaque) || !o->now(o->opaque, &start) ||
        start > ~(QbwTime)0 - QBW_DEADLINE_NS - QBW_TERM_GRACE_NS) {
        return r;
    }

    r.internal_error = 0;

    if (o->spawn(o->opaque, &pid)) {
        r.code = QBW_SPAWN_FAILED;

        return r;
    }

    r.spawned = 1;

    if (pid <= 0) {
        r.internal_error = 1;
        r.cleanup_unverified = 1;

        return r;
    }

    previous = start;

    if (!o->now(o->opaque, &start) || start < previous ||
        start > ~(QbwTime)0 - QBW_DEADLINE_NS - QBW_TERM_GRACE_NS) {
        r.internal_error = 1;
        qbw_cleanup(o, pid, &r);

        return r;
    }

    deadline = start + QBW_DEADLINE_NS;
    previous = start;

    for (;;) {
        int state = o->poll(o->opaque, pid, &code);
        ++r.polls;

        if (state == 1) {
            r.child_reaped = 1;
            r.internal_error = !o->owner(o->opaque);
            r.code = r.internal_error ? QBW_INTERNAL : code;

            return r;
        }

        if (state == -1) {
            r.internal_error = 1;
            r.cleanup_unverified = 1;

            return r;
        } /* ECHILD is never success; no reused-PID signal. */

        if (state < 0) {
            r.internal_error = 1;
            break;
        }

        if (!o->owner(o->opaque) || !o->now(o->opaque, &now) || now < previous) {
            r.internal_error = 1;
            break;
        }

        previous = now;

        if (now >= deadline) {
            r.timed_out = 1;
            break;
        }

        QbwTime delay = r.polls <= 10 ? 10000000ULL : 100000000ULL;

        if (delay > deadline - now) {
            delay = deadline - now;
        }

        if (o->pause(o->opaque, delay) < 0) {
            r.internal_error = 1;
            break;
        }
    }

    if (!r.timed_out) {
        qbw_cleanup(o, pid, &r);

        return r;
    }

    if (now > ~(QbwTime)0 - QBW_TERM_GRACE_NS) {
        r.internal_error = 1;
        qbw_cleanup(o, pid, &r);

        return r;
    }

    if (o->signal(o->opaque, pid, 1) < 0) {
        r.internal_error = 1;
    }

    QbwTime grace = now + QBW_TERM_GRACE_NS;
    previous = now;

    for (;;) {
        int state = o->poll(o->opaque, pid, &code);
        ++r.polls;

        if (state == 1) {
            r.child_reaped = 1;

            if (!o->owner(o->opaque)) {
                r.internal_error = 1;
            }

            r.code = r.internal_error ? QBW_INTERNAL : QBW_TIMEOUT;

            return r;
        }

        if (state == -1) {
            r.internal_error = 1;
            r.cleanup_unverified = 1;

            return r;
        }

        if (state < 0) {
            r.internal_error = 1;
            break;
        }

        if (!o->owner(o->opaque) || !o->now(o->opaque, &now) || now < previous) {
            r.internal_error = 1;
            break;
        }

        previous = now;

        if (now >= grace) {
            break;
        }

        QbwTime delay = grace - now;

        if (delay > 10000000ULL) {
            delay = 10000000ULL;
        }

        if (o->pause(o->opaque, delay) < 0) {
            r.internal_error = 1;
            break;
        }
    }

    qbw_cleanup(o, pid, &r);
    r.code = r.internal_error ? QBW_INTERNAL : QBW_TIMEOUT;

    return r;
}

#endif
