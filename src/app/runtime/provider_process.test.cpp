#include "provider_process.hpp"
#include "provider_process_test_host.hpp"
#include <cassert>
#include <fstream>
#include <iostream>
#include <stdexcept>
using namespace voyemu;
using namespace voyemu::runtime;

int main(int argc, char **argv) try {
    if (const auto code = provider_child_mode(argc, argv)) {
        return *code;
    }

    provider_lifecycle_case(argc, argv);
    auto sample = provider_sample({{"status", "ready"},
                                   {"source_instance", "s"},
                                   {"boot_id", "b"},
                                   {"revision", "r"},
                                   {"data", {{"enabled", false}}}});
    assert(sample.status == "ready" && !sample.continuous &&
           sample.value["data"]["enabled"] == false);
    assert(provider_sample({{"status", "absent"}}).status == "ready");
    assert(provider_sample({{"status", "unavailable"}}).status == "unavailable");
} catch (const std::exception &error) {
    std::cerr << error.what() << '\n';

    return 1;
}
