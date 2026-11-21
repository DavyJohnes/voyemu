#include "provider_registry_test_host.hpp"
#include <cassert>
#include <fstream>
#include <sys/stat.h>

void public_result(const std::filesystem::path &path, const voyemu::Json &reply) {
    {
        std::ofstream file(path);
        file << reply.dump();
    }
    assert(chmod(path.c_str(), 0644) == 0);
}

void pinned_self_worker_case(const voyemu::Json &, const std::filesystem::path &,
                             const std::filesystem::path &, const std::filesystem::path &,
                             const voyemu::runtime::ProviderContext &,
                             const std::function<void()> &) {}
