#pragma once
#include "model.hpp"
#include "process.hpp"
#include <functional>
#include <map>
namespace voyemu::runtime {
struct ProviderContext {
    std::string request_id, id, profile, guest_serial, boot_id;
};
// Trusted operator-registered code executes as the current OS user, without an OS sandbox.
// Runner must enforce the supplied remaining deadline and terminate descendants before return.
using ProviderRunner = std::function<ProcessResult(const ProcessSpec &, int)>;
void ensure_provider_jobs_directory(const std::filesystem::path &jobs);
class ProviderRegistry {
public:
    explicit ProviderRegistry(std::filesystem::path jobs) : jobs_(std::move(jobs)) {}

    void load(const Json &manifest);
    bool contains(const std::string &id) const;
    Json capabilities() const;
    Json collect(const std::string &id, const ProviderContext &context, int remaining_ms,
                 const ProviderRunner &runner, const std::function<bool()> &fence) const;

private:
    std::filesystem::path jobs_;
    std::map<std::string, Json> entries_;
};
} // namespace voyemu::runtime
