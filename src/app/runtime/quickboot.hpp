#pragma once
#include "model.hpp"
#include <functional>
#include <optional>

namespace voyemu {
struct QuickBootRestore {
    bool resume = false;
    std::string boot_nonce, reason;
};
bool android_presave_required(const Json &specification);
std::optional<Json> android_presave_snapshot(
        const std::string &session, const std::string &guest_boot, std::uint32_t sequence,
        const std::function<Json(const std::string &)> &presave, const std::function<bool()> &save);
Json cache_file_identity(const std::filesystem::path &path, std::uintmax_t expected_links = 1);
Json android_quickboot_key(const Json &specification);
std::string android_quickboot_config(std::string config);
class AndroidQuickBoot {
public:
    static constexpr const char *snapshot_name = "voyemu-last";
    AndroidQuickBoot(std::filesystem::path machine, Json key, std::filesystem::path runtime = {});
    QuickBootRestore inspect() const;
    QuickBootRestore runtime_checkpoint()
            const; // Ordinary start checks disk identities without walking the snapshot.
    // Read-only reserve for a complete replacement snapshot; unknown space is not permission to
    // write.
    Json snapshot_save_budget(std::optional<std::uint64_t> available) const;
    void begin();
    void commit(const std::string &boot_nonce, const std::string &session = {},
                const Json &presave = Json(), const std::string &guest_boot = {},
                std::uint32_t input_sequence = 0);

private:
    Json disks() const;
    Json snapshot() const;
    std::filesystem::path machine_, runtime_, record_;
    Json key_;
    std::string generation_;
};
} // namespace voyemu
