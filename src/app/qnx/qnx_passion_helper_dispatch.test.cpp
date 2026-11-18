#include <cassert>
#include <exception>
#include <initializer_list>
static int phase;
static bool bounded, network;
struct Exit : std::exception {};

static void qbw_start() {
    assert(!phase);
    phase = 1;

    if (bounded) {
        throw Exit{};
    }
}

static void qnet_start() {
    assert(phase == 1);
    phase = 2;

    if (network) {
        throw Exit{};
    }
}

#include "../../guest/qnx/passion_helper_dispatch.h"

int main() {
    for (bool b : {false, true}) {
        for (bool n : {false, true}) {
            phase = 0;
            bounded = b;
            network = n;
            bool exited = false;
            try {
                QGFX_HELPER_DISPATCH();
            } catch (const Exit &) {
                exited = true;
            }

            assert(exited == (b || n) && phase == (b ? 1 : 2));
        }
    }
}
