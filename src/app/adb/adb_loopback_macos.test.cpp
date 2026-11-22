#include "adb_loopback_test_host.hpp"
#include <malloc/malloc.h>

std::size_t heap_bytes_in_use() {
    malloc_statistics_t statistics{};

    malloc_zone_statistics(nullptr, &statistics);

    return statistics.size_in_use;
}
