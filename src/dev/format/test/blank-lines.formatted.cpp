// Formatter fixture: blank lines between top-level and member definitions.
#include <string>

namespace voyemu {
namespace {
constexpr unsigned maximum_width = 3840, maximum_height = 2160;
using Clock = int;

void need(bool condition, const char *reason) {
    if (!condition) {
        throw reason;
    }
}

// A comment directly above a function belongs to it.
const char *codec_host() {
    return "host";
}

bool pinned(int value) {
    int copy = value;

    copy++;

    return copy > 0;
}

void first_stub() {}

void second_stub() {}

int declared(int value);
int also_declared(int value);
int counter = 0;
} // namespace

class Point {
public:
    Point() {}

    int sum() const {
        return x_ + y_;
    }

    int difference() const {
        return x_ - y_;
    }

    void reset();
    void move(int dx);

private:
    int x_ = 0;
    int y_ = 0;
};

#ifdef _WIN32
int platform() {
    return 1;
}
#else
int platform() {
    return 2;
}
#endif

template <typename T> T twice(T value) {
    return value + value;
}

int local_types() {
    struct Local {
        int a;
    };
    Local local{};
    int other = 2;

    other++;

    return local.a + other;
}
} // namespace voyemu

extern "C" {
int c_entry(void) {
    return 0;
}

int c_other(void) {
    return 1;
}
}
