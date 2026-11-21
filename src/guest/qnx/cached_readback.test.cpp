#include <cassert>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <vector>
extern "C" {
void glDrawElements(unsigned, int, unsigned, const void *);
unsigned glGetError(void);
void qgfx_test_bind_element_buffer(unsigned), qgfx_test_fail_transport(void);
const void *qgfx_test_present(int, int);
}
namespace {
using U64 = unsigned long long;
constexpr U64 element_buffer_bytes = 144, readback_blob = 7;
U64 next_physical = 0x40000000;
void *registered = nullptr;
unsigned char shade = 0;
bool short_readback = false;
std::vector<unsigned> ops;
std::vector<U64> draw;
} // namespace

extern "C" void *qgfx_test_malloc(unsigned long n) {
    return std::malloc(n);
}

extern "C" void qgfx_test_free(void *p) {
    std::free(p);
}

extern "C" int qgfx_test_mlock(const void *, unsigned long) {
    return 0;
}

extern "C" int qgfx_test_munlock(const void *, unsigned long) {
    return 0;
}

extern "C" int qgfx_test_mem_offset(const void *p, int, unsigned long n, long long *out,
                                    unsigned long *span) {
    registered = const_cast<void *>(p);
    *out = static_cast<long long>(next_physical);
    *span = n;
    next_physical += static_cast<U64>((n + 4095) / 4096) * 4096;

    return 0;
}

extern "C" int qgfx_test_snprintf(char *, unsigned long, const char *, ...) {
    return 0;
}

extern "C" long qgfx_test_write(int, const void *, unsigned long n) {
    return static_cast<long>(n);
}

extern "C" int qgfx_test_usleep(unsigned) {
    return 0;
}

extern "C" U64 test_remote(void *, unsigned op, const U64 *a, unsigned argc, const void *,
                           unsigned) {
    ops.push_back(op);

    switch (op) {
    case 20:
        return 0;
    case 40:
        return readback_blob;
    case 41:
    case 42:
        return 1;
    case 352:
        // The host rejects indices beyond the bound element buffer without reading them; the
        // guest sees the faulted reply as a transport error.
        if (a[0] * 4 + a[2] > element_buffer_bytes) {
            qgfx_test_fail_transport();

            return 0;
        }

        return 4;
    case 133:
        draw.assign(a, a + argc);

        return 1;
    case 24: {
        assert(argc == 7 && a[6] == readback_blob && registered);
        const auto bytes = a[2] * a[3] * 4;

        if (short_readback) {
            return bytes - 1;
        }

        std::memset(registered, shade, bytes);

        return bytes;
    }
    default:
        assert(false);

        return 0;
    }
}

int main() {
    qgfx_test_bind_element_buffer(1);
    // 192 unsigned indices at offset 312 exceed the 144-byte element buffer: nothing is drawn.
    glDrawElements(4, 192, 0x1405, reinterpret_cast<const void *>(312));
    assert(ops == std::vector<unsigned>{352} && draw.empty());
    glDrawElements(4, 6, 0x1405, nullptr);
    assert(ops.back() == 133 && (draw == std::vector<U64>{4, 6, 0x1405, 0}));

    shade = 34;
    const auto *frame = static_cast<const unsigned char *>(qgfx_test_present(4, 2));
    assert(frame && frame[0] == 34 && frame[31] == 34);
    assert(glGetError() == 0x502 && glGetError() == 0);

    short_readback = true;
    assert(!qgfx_test_present(4, 2));
    short_readback = false;
    shade = 68;
    frame = static_cast<const unsigned char *>(qgfx_test_present(4, 2));
    assert(frame && frame[0] == 68 && frame[31] == 68);
    assert(glGetError() == 0x502 && glGetError() == 0);
    std::cout << "guest present readback: rejected draw skipped, later frames published, failed "
                 "readback reported once\n";
}
