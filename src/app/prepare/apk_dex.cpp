#include "apk_dex.hpp"
#include "check.hpp"
#include "error.hpp"
#include "process.hpp"
#include "zip_archive.hpp"
#include <map>
#include <set>
#include <stdexcept>
namespace voyemu {
namespace {
constexpr Check require{"APK_DEX"};

std::span<const std::byte> bytes(const std::string &value) {
    return std::as_bytes(std::span<const char>(value.data(), value.size()));
}
} // namespace

ApkDexAppendResult append_pinned_apk_dex(const std::filesystem::path &source,
                                         const std::string &source_sha256,
                                         std::span<const ApkDexPin> original,
                                         std::span<const ApkDexTemplate> templates,
                                         const std::filesystem::path &destination,
                                         std::uint64_t reserve_bytes) {
    require(original.size() <= 65535 && templates.size() <= 65535 - original.size(),
            "too many DEX entries");
    require(!templates.empty(), "no templates");
    const auto inventory = pinned_zip_inventory(source, source_sha256);
    std::map<std::string, std::string> pins;
    std::set<std::string> observed;

    for (const auto &pin : original) {
        require(pin.entry.size() <= 32 && pins.emplace(pin.entry, pin.sha256).second,
                "duplicate or oversized DEX pin");
    }

    for (const auto &entry : inventory) {
        if (entry.name.find('/') == std::string::npos && entry.name.starts_with("classes") &&
            entry.name.ends_with(".dex")) {
            require(!entry.directory && pins.contains(entry.name), "unaccounted original DEX");
            observed.insert(entry.name);
        }
    }

    require(observed.size() == pins.size(), "original DEX pin not in APK");
    std::vector<std::string> originals;
    originals.reserve(original.size());
    std::size_t total = 0;

    for (const auto &pin : original) {
        auto data = read_pinned_zip_entry(source, source_sha256, pin.entry, pin.sha256);
        total += data.size();
        require(total <= 256 * 1024 * 1024, "DEX payload budget");
        originals.push_back(std::move(data));
    }

    std::vector<DexInput> inputs;
    inputs.reserve(original.size());

    for (std::size_t i = 0; i < original.size(); ++i) {
        inputs.push_back({original[i].entry, bytes(originals[i])});
    }

    std::vector<DexTemplate> owned;
    owned.reserve(templates.size());

    for (const auto &component : templates) {
        require(component.bytes.size() <= 32 * 1024 * 1024, "template extent");
        total += component.bytes.size();
        require(total <= 256 * 1024 * 1024, "DEX payload budget");
        require(sha256_bytes(component.bytes) == component.sha256, "template SHA mismatch");
        owned.push_back({bytes(component.bytes), component.expected_classes});
    }

    auto slots = plan_dex_append(inputs, owned);
    std::vector<ZipAddition> additions;
    additions.reserve(slots.size());

    for (std::size_t i = 0; i < slots.size(); ++i) {
        additions.push_back({slots[i].entry, templates[i].bytes, templates[i].sha256});
    }

    auto hash = rewrite_pinned_zip(source, source_sha256, {}, {}, additions, destination,
                                   reserve_bytes);

    return {std::move(hash), std::move(slots)};
}
} // namespace voyemu
