#include "adb_loopback_test_host.hpp"
#include <malloc.h>

std::size_t heap_bytes_in_use() {
    const auto statistics = mallinfo2();

    return statistics.uordblks + statistics.hblkhd;
}
