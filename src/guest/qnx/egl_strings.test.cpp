#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <map>
#include <string>
#include <vector>
extern "C" {
const unsigned char *glGetString(unsigned);
const char *eglQueryString(void *, int);
unsigned glGetError(void);
void qgfx_test_reset_error(void), qgfx_test_fail_transport(void), qgfx_test_lose_download(void);
int qgfx_test_error(void);
}
namespace {
using U64 = unsigned long long;
struct Packet {
    unsigned magic, version, state, opcode, seq, in_len, out_len, fault;
    U64 args[16], result, tid, reserved[10];
    unsigned char data[3840];
};
static_assert(offsetof(Packet, data) == 256 && sizeof(Packet) == 4096);
struct Memory {
    void *pointer;
    unsigned long size;
};
std::map<U64, Memory> memory;
struct Blob {
    unsigned long size;
    std::vector<Memory> runs;
};
std::map<U64, Blob> blobs;
U64 next = 0x40000000, next_blob = 0;
bool fail_registration = false;
unsigned error = 0;
enum class Mode {
    normal,
    bad_nul,
    oversize,
    inline_old_host,
    partial_fault,
    lost_download,
    short_count
};
Mode mode = Mode::normal;
std::string extensions, messages;
unsigned freed = 0;
} // namespace

extern "C" void *qgfx_test_malloc(unsigned long n) {
    return std::malloc(n);
}

extern "C" void qgfx_test_free(void *p) {
    if (p) {
        ++freed;
    }

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
    *out = next;
    *span = n;
    memory[next] = {const_cast<void *>(p), n};
    next += static_cast<U64>((n + 4095) / 4096) * 4096;

    return 0;
}

extern "C" int qgfx_test_snprintf(char *, unsigned long, const char *, ...) {
    return 0;
}

extern "C" long qgfx_test_write(int, const void *p, unsigned long n) {
    messages.append(static_cast<const char *>(p), n);

    return static_cast<long>(n);
}

extern "C" int qgfx_test_usleep(unsigned) {
    return 0;
}

extern "C" U64 test_remote(void *raw, unsigned op, const U64 *a, unsigned argc, const void *input,
                           unsigned bytes) {
    auto &p = *static_cast<Packet *>(raw);

    p.out_len = 0;
    p.fault = 0;

    if (op == 40) {
        if (fail_registration) {
            return 0;
        }

        auto id = ++next_blob;
        blobs[id] = {static_cast<unsigned long>(a[0]), {}};

        return id;
    }

    if (op == 41) {
        auto &b = blobs.at(a[0]);
        assert(bytes == a[1] * 16);
        auto *d = static_cast<const U64 *>(input);

        for (unsigned i = 0; i < a[1]; i++) {
            auto m = memory.at(d[static_cast<std::size_t>(i) * 2]);
            assert(d[i * 2 + 1] <= m.size);
            m.size = d[i * 2 + 1];
            b.runs.push_back(m);
        }

        return 1;
    }

    if (op == 42) {
        blobs.erase(a[0]);

        return 1;
    }

    if (op == 20) {
        auto v = error;
        error = 0;

        return v;
    }

    if (op == 21 || op == 12) {
        if (argc < (op == 12 ? 3u : 2u)) {
            qgfx_test_fail_transport();

            return 0;
        }

        unsigned key = static_cast<unsigned>(a[op == 12 ? 1 : 0]);

        if (key == 0xdead) {
            error = 0x500;

            return 0;
        }

        const std::string value = key == 0x1f03 || key == 0x3055 ? extensions : "driver";

        if (mode == Mode::inline_old_host) {
            p.out_len = 1;

            return 1;
        }

        auto &b = blobs.at(a[op == 12 ? 2 : 1]);
        assert(b.size >= value.size() + 1);
        std::string data = value + '\0';

        if (mode == Mode::bad_nul) {
            data.back() = 'x';
        }

        const auto limit = mode == Mode::partial_fault ? std::size_t(100) : data.size();
        std::size_t at = 0;

        for (auto m : b.runs) {
            auto n = std::min<std::size_t>(m.size, limit - at);
            std::memcpy(m.pointer, data.data() + at, n);
            at += n;

            if (at == limit) {
                break;
            }
        }

        assert(at == limit);

        if (mode == Mode::partial_fault) {
            qgfx_test_fail_transport();

            return 0;
        }

        if (mode == Mode::lost_download) {
            qgfx_test_lose_download();
        }

        if (mode == Mode::oversize) {
            return 65537;
        }

        if (mode == Mode::short_count) {
            return data.size() - 1;
        }

        return data.size();
    }

    assert(false);

    return 0;
}

int main() {
    extensions.assign(4733, 'x');

    for (std::size_t i = 0; i < extensions.size(); ++i) {
        extensions[i] = char('a' + i % 26);
    }

    const auto *p = glGetString(0x1f03);
    assert(p && std::string(reinterpret_cast<const char *>(p)) == extensions);
    assert(glGetString(0x1f03) == p);
    assert(std::string(eglQueryString(nullptr, 0x3055)) == extensions);
    assert(!glGetString(0xdead) && !qgfx_test_error() && glGetError() == 0x500);

    for (auto failure : {Mode::bad_nul, Mode::oversize, Mode::inline_old_host, Mode::partial_fault,
                         Mode::lost_download, Mode::short_count}) {
        mode = Mode::normal;
        assert(glGetString(0x1f03));
        mode = failure;
        assert(!glGetString(0x1f03) && qgfx_test_error());
        qgfx_test_reset_error();
        assert(blobs.empty());
    }

    assert(messages.find("HOST_STRING_BULK_UNSUPPORTED") != std::string::npos);
    mode = Mode::normal;
    fail_registration = true;
    auto before = freed;
    assert(!glGetString(0x1f03) && qgfx_test_error() && freed > before);
    qgfx_test_reset_error();
    fail_registration = false;
    assert(blobs.empty());
    // An earlier rejected command stays reported but must not fail a later query.
    qgfx_test_fail_transport();
    p = glGetString(0x1f03);
    assert(p && std::string(reinterpret_cast<const char *>(p)) == extensions && qgfx_test_error());
    assert(glGetError() == 0x502 && glGetError() == 0 && blobs.empty());
    std::cout
            << "guest string wrapper: complete4733, stable lifetime, bad NUL/count/partial failure/lost download/old host/registration guarded, earlier error kept\n";
}
