#include "passion_watch_clock.hpp"
#include <cassert>
#include <exception>
#include <iostream>
using voyemu::media::PassionWatchClock;

template <class F> void rejects(F f) {
    bool bad = false;

    try {
        f();
    } catch (...) {
        bad = true;
    }

    assert(bad);
}

int main() try {
    PassionWatchClock c;

    c.launched(1000000);
    assert(c.event(100301, 1010000, 100301) == 100311);
    assert(c.event(101301, 2010000, 101301) == 101311);
    assert(c.upper(2010000) == 101311);
    rejects([&] {
        c.event(120000, 2010000, 120000);
    });
    rejects([&] {
        c.event(99301, 3010000, 99301);
    });
    rejects([&] {
        c.event(101300, 3010000, 101300);
    });
    rejects([&] {
        c.upper(0);
    });
    rejects([&] {
        c.upper(122010001);
    });
    rejects([&] {
        c.event(101301, 1900000, 101301);
    });
    rejects([&] {
        c.upper(1900000);
    });
    c.reset();
    rejects([&] {
        c.upper(2010000);
    });
    PassionWatchClock late;
    late.launched(0);
    rejects([&] {
        late.event(1000, 1500001, 1000);
    });
    PassionWatchClock forged;
    forged.launched(0);
    rejects([&] {
        forged.event(9999999, 1, 1000);
    }); // Continuous validated frames renew liveness without moving the original clock mapping.
    PassionWatchClock continuous;
    continuous.launched(1000000);

    for (std::uint64_t second = 0; second <= 600; ++second) {
        const auto receipt = 1010000 + second * 1000000;
        const auto guest = 100301 + static_cast<std::int64_t>(second * 1000);
        assert(continuous.event(guest, receipt, 100301) == guest + 10);
        assert(continuous.upper(receipt) == guest + 10);
    }

    // Neither replay nor bounded-but-cumulative drift may become a new clock anchor.
    rejects([&] {
        continuous.event(700300, 602010000, 100301);
    });
    rejects([&] {
        continuous.event(702000, 602010000, 100301);
    });
    assert(continuous.event(701301, 602010000, 100301) == 701311);
    assert(continuous.upper(722010000) == 821311);
    rejects([&] {
        continuous.upper(722010001);
    });
    rejects([&] {
        continuous.event(821301, 722010001, 100301);
    });
    // A rejected future frame cannot extend the last validated receipt's lifetime.
    PassionWatchClock invalid;
    invalid.launched(0);
    assert(invalid.event(10000, 10000, 10000) == 10010);
    rejects([&] {
        invalid.event(999999, 119010000, 10000);
    });
    rejects([&] {
        invalid.upper(120010001);
    });
    // The admitted epoch's upper projection is safe at the maximum live interval.
    PassionWatchClock edge;
    edge.launched(0);
    const auto epoch = std::numeric_limits<std::int64_t>::max() - 1000000;
    assert(edge.event(epoch, 0, epoch) == epoch);
    assert(edge.upper(120000000) == epoch + 120000);
    std::cout << "clock offset301 mapped, late/future/regression/expiry/reset rejected PASS\n";
} catch (const std::exception &error) {
    std::cerr << error.what() << '\n';

    return 1;
}
