#include "model_fixture.h"
#include <assert.h>
#include <string.h>
int main(void)
{
    const char valid[] = "voyemu-sim-v1 2 2 2 2 1 1 0 2\n";
    const char invalid[] = "voyemu-sim-v1 2 2 2 4 1 1 0 2\n";
    const char bad_acc[] = "voyemu-sim-v1 2 2 2 2 1 1 0 0\n";
    const char extra[] = "voyemu-sim-v1 2 2 2 2 1 1 0 2\nX";
    struct mcu_model m = {0}, old;
    uint8_t drive = 0; int32_t acc = -1;
    assert(model_fixture_parse(valid, sizeof(valid)-1, &m, &drive, &acc));
    assert(m.forced_known && m.warning_known && m.forced == 2 && m.warning == 2);
    assert(m.eps == 2 && m.retention == 1 && m.propulsion == 1 && !m.daily_repeat && drive == 2 && acc == 2);
    old = m;
    assert(!model_fixture_parse(invalid, sizeof(invalid)-1, &m, &drive, &acc));
    assert(!memcmp(&m, &old, sizeof(m)) && drive == 2 && acc == 2);
    assert(model_fixture_parse(bad_acc, sizeof(bad_acc)-1, &m, &drive, &acc) && acc == 0);
    assert(model_fixture_parse(valid, sizeof(valid)-1, &m, &drive, &acc) && acc == 2);
    assert(!model_fixture_parse(extra, sizeof(extra)-1, &m, &drive, &acc));
    for (size_t i = 0; i < sizeof(valid)-1; ++i)
        assert(!model_fixture_parse(valid, i, &m, &drive, &acc));
    return 0;
}
