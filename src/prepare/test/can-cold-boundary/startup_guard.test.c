#include "startup_guard.h"
#include <assert.h>
int main(void) {
    assert(startup_ready("1"));
    assert(!startup_ready("") && !startup_ready("0") && !startup_ready(NULL));
    assert(startup_fixture_allowed(1, 0, "0", 1, ""));
    assert(!startup_fixture_allowed(1, 0, "<absent>", 0, "0"));
    assert(!startup_fixture_allowed(1, -1, "", 0, "0"));
    assert(!startup_fixture_allowed(0, 1, "", 1, ""));
    assert(!startup_fixture_allowed(1, 0, "1", 0, "1"));
    assert(!startup_fixture_allowed(1, 1, "", 0, "1"));
    assert(!startup_fixture_allowed(1, 0, "", 0, "0"));
    return 0;
}
