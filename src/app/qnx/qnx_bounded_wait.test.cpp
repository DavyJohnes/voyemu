#include "../../guest/qnx/bounded_wait.h"
#include <cassert>
#include <exception>
#include <iostream>
#include <vector>
struct Mock {
    QbwTime time = 0, exit_at = 0;
    int status = 0, spawn_error = 0, poll_error = 0, interrupts = 0, owner_ok = 1, clock_error = 0,
        reap_error = 0, kill_error = 0, reap_calls = 0;
    bool spawned = false, reaped = false, term_exits = false, kill_error_exited = false;
    std::vector<int> signals;
    std::vector<QbwTime> delays;

    static int now(void *p, QbwTime *t) {
        auto &m = *(Mock *)p;

        if (m.clock_error) {
            return 0;
        }

        *t = m.time;

        return 1;
    }

    static int owner(void *p) {
        return ((Mock *)p)->owner_ok;
    }

    static int spawn(void *p, int *pid) {
        auto &m = *(Mock *)p;

        if (m.spawn_error) {
            return 1;
        }

        m.spawned = true;
        *pid = 73;

        return 0;
    }

    static int poll(void *p, int pid, int *code) {
        auto &m = *(Mock *)p;
        assert(pid == 73);

        if (m.poll_error) {
            return -1;
        }

        if (m.interrupts) {
            --m.interrupts;

            return 2;
        }

        if (m.time >= m.exit_at) {
            m.reaped = true;
            *code = m.status;

            return 1;
        }

        return 0;
    }

    static int pause(void *p, QbwTime t) {
        auto &m = *(Mock *)p;
        assert(t && t <= 100000000ULL);
        m.delays.push_back(t);
        m.time += t;

        return 0;
    }

    static int signal(void *p, int pid, int sig) {
        auto &m = *(Mock *)p;
        assert(pid == 73 && !m.reaped);
        m.signals.push_back(sig);

        if (sig == 2 && m.kill_error) {
            if (m.kill_error_exited) {
                m.exit_at = m.time;
            }

            return -1;
        }

        if (sig == 1 && m.term_exits) {
            m.exit_at = m.time;
        }

        return 0;
    }

    static int reap(void *p, int pid) {
        auto &m = *(Mock *)p;
        assert(pid == 73);
        ++m.reap_calls;
        m.reaped = true;

        return m.reap_error;
    }

    QbwOps ops() {
        return {this, now, owner, spawn, poll, pause, signal, reap};
    }

    QbwResult run() {
        auto o = ops();

        return qbw_run(&o);
    }
};

int main() try {
    {
        Mock m;
        m.status = 7;
        auto r = m.run();
        assert(r.code == 7 && m.reaped && m.signals.empty() && m.delays.empty());
    }
    {
        Mock m;
        m.exit_at = 50000000;
        m.status = 143;
        auto r = m.run();
        assert(r.code == 143 && !r.timed_out && m.signals.empty());
    }
    {
        Mock m;
        m.exit_at = ~QbwTime(0);
        auto r = m.run();
        assert(r.code == 124 && r.timed_out && m.reaped);
        assert((m.signals == std::vector<int>{1, 2}));
        assert(m.time == QBW_DEADLINE_NS + QBW_TERM_GRACE_NS);
    }
    {
        Mock m;
        m.exit_at = ~QbwTime(0);
        m.term_exits = true;
        auto r = m.run();
        assert(r.code == 124 && m.time == QBW_DEADLINE_NS);
        assert((m.signals == std::vector<int>{1}));
    }
    {
        Mock m;
        m.spawn_error = 1;
        auto r = m.run();
        assert(r.code == 127 && !r.spawned && m.signals.empty());
    }
    {
        Mock m;
        m.poll_error = 1;
        auto r = m.run();
        assert(r.code == 125 && r.internal_error && !m.reaped && m.signals.empty());
    }
    {
        Mock m;
        m.interrupts = 3;
        m.exit_at = 20000000;
        auto r = m.run();
        assert(r.code == 0 && m.reaped && m.signals.empty());
    }
    {
        Mock m;
        m.owner_ok = 0;
        auto r = m.run();
        assert(r.code == 125 && !m.spawned);
    }
    {
        Mock m;
        m.clock_error = 1;
        auto r = m.run();
        assert(r.code == 125 && !m.spawned);
    }
    {
        Mock m;
        m.time = ~QbwTime(0) - 1000;
        auto r = m.run();
        assert(r.code == 125 && !m.spawned);
    }
    {
        Mock m;
        m.exit_at = ~QbwTime(0);
        m.reap_error = 1;
        auto r = m.run();
        assert(r.code == 125 && r.internal_error);
    }
    {
        Mock m;
        m.exit_at = ~QbwTime(0);
        m.kill_error = 1;
        m.kill_error_exited = true;
        auto r = m.run();
        assert(r.code == 125 && r.child_reaped && !r.cleanup_unverified && m.reap_calls == 0);
    }
    {
        Mock m;
        m.exit_at = ~QbwTime(0);
        m.kill_error = 1;
        auto r = m.run();
        assert(r.code == 125 && !r.child_reaped && r.cleanup_unverified && m.reap_calls == 0);
    }
    std::cout << "bounded wait 40.1s + 100ms TERM grace host mocks passed\n";
} catch (const std::exception &error) {
    std::cerr << error.what() << '\n';

    return 1;
}
