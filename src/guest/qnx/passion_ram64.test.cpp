#include "passion_ram64.h"
#include <cassert>
#include <initializer_list>
static unsigned calls;
static int failure, error_code;
static void *expected_addr;
static Qpr64Size expected_len = 0x4b28e000UL;
static int expected_prot = 0xb00, expected_flags = 0x10001;
static Qpr64Physical expected_pa = 0x27c008000ULL;

static void *real_map(void *a, Qpr64Size n, int p, int f, int fd, Qpr64Physical pa) {
    ++calls;
    assert(a == expected_addr && n == expected_len && p == expected_prot && f == expected_flags &&
           fd == -1 && pa == expected_pa);

    if (failure) {
        error_code = 13;

        return (void *)-1;
    }

    return (void *)0x12340000UL;
}

int main() {
    const char *key = "passion-20260403-2.1.4.80-system-ram64-v1";
    auto invoke = [&](const char *k, void *a, Qpr64Size n, int p, int f, Qpr64Physical t,
                      Qpr64Map fn) {
        return qpr64_bind(k, a, n, p, f, t, fn, &error_code);
    };

    assert(invoke(key, nullptr, 0x4b28e000UL, 0xb00, 0, 0x7c008000ULL, real_map) ==
                   (void *)0x12340000UL &&
           calls == 1);
    failure = 1;
    assert(invoke(key, nullptr, 0x4b28e000UL, 0xb00, 0, 0x7c008000ULL, real_map) == (void *)-1 &&
           error_code == 13 && calls == 2);
    failure = 0;

    for (const char *bad : {"", "free", "passion-20260403-2.1.4.80-system-ram64-v2"}) {
        assert(invoke(bad, nullptr, 0x4b28e000UL, 0xb00, 0, 0x7c008000ULL, real_map) ==
                       (void *)-1 &&
               error_code == 1);
    }

    assert(invoke(key, (void *)1, 0x4b28e000UL, 0xb00, 0, 0x7c008000ULL, real_map) == (void *)-1);
    assert(invoke(key, nullptr, 0xa0000001UL, 0xb00, 0, 0x7c008000ULL, real_map) == (void *)-1);
    assert(invoke(key, nullptr, 0x4b28e000UL, 0xb01, 0, 0x7c008000ULL, real_map) == (void *)-1);
    assert(invoke(key, nullptr, 0x4b28e000UL, 0xb00, 1, 0x7c008000ULL, real_map) == (void *)-1);
    assert(invoke(key, nullptr, 0x4b28e000UL, 0xb00, 0, 0x27c008000ULL, real_map) == (void *)-1);
    assert(invoke(key, nullptr, 0x4b28e000UL, 0xb00, 0, 0x7c009000ULL, real_map) == (void *)-1);
    assert(invoke(key, nullptr, 0x4b28e000UL, 0xb00, 0, 0x7c008000ULL, nullptr) == (void *)-1 &&
           calls == 2);
    assert(qpr64_bind(key, nullptr, 0x4b28e000UL, 0xb00, 0, 0x7c008000ULL, real_map, nullptr) ==
                   (void *)-1 &&
           calls == 2);
    // Absent binding reproduces the ORIGINAL libc wrapper, including every flag bit.
    expected_addr = (void *)0x40000000UL;
    expected_len = 0x1234000;
    expected_prot = 0x12345;
    expected_pa = 0x123456789abcULL;

    for (unsigned bit = 0; bit < 33; ++bit) {
        const unsigned flags = bit == 32 ? 0xffffffffU : 1U << bit;
        expected_flags = (int)((flags & 0xfffefff0U) | 0x10001U);
        assert(invoke(nullptr, expected_addr, expected_len, expected_prot, (int)flags, expected_pa,
                      real_map) == (void *)0x12340000UL);
    }

    failure = 1;
    expected_flags = 0x10001;
    assert(invoke(nullptr, expected_addr, expected_len, expected_prot, 0, expected_pa, real_map) ==
                   (void *)-1 &&
           error_code == 13);
    const auto before = calls;
    assert(invoke(nullptr, nullptr, 0, 0, 0, 0, nullptr) == (void *)-1 && calls == before &&
           error_code == 22);
}
