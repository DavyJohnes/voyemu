#include "qnx_checkpoint.hpp"
#include "check.hpp"
#include "error.hpp"
#include "process.hpp"
#include "qnx_interguest_contract.hpp"
#include "quickboot.hpp"
#include <algorithm>
#include <array>
#include <cerrno>
#include <charconv>
#include <fstream>
#include <map>
#include <optional>
#include <regex>
#include <set>
#include <sstream>
#ifdef __APPLE__
#include <fcntl.h>
#include <sys/clonefile.h>
#include <sys/mount.h>
#endif
#ifndef _WIN32
#include <sys/stat.h>
#include <unistd.h>
#endif
namespace voyemu {
namespace {
void owned_file(const std::filesystem::path &path) {
    if (!std::filesystem::is_regular_file(std::filesystem::symlink_status(path)) ||
        std::filesystem::hard_link_count(path) != 1) {
        throw VoyemuError("QNX_CACHE", "ordinary owned file required");
    }

#ifndef _WIN32
    struct stat st{};

    if (::lstat(path.c_str(), &st) || st.st_uid != ::geteuid() || (st.st_mode & 0077)) {
        throw VoyemuError("QNX_CACHE", "private file required");
    }
#endif
}

constexpr const char *verification_name = "verified-files.json";
constexpr std::uint64_t ram_base = 0x40000000ULL, ram_bytes = 8ULL * 1024 * 1024 * 1024;

void validate_boot_roms(const Json &ranges) {
    if (!ranges.is_array() || ranges.empty() || ranges.size() > 8) {
        throw VoyemuError("QNX_CACHE_RAM", "bounded ROM list required");
    }

    std::uint64_t previous = ram_base, total = 0;

    for (const auto &item : ranges) {
        if (!item.is_object() || item.size() != 2 || !item.contains("address") ||
            !item.contains("bytes") || !item.at("address").is_number_integer() ||
            !item.at("bytes").is_number_integer()) {
            throw VoyemuError("QNX_CACHE_RAM", "invalid ROM record");
        }

        const auto address = item.at("address").get<std::uint64_t>(),
                   bytes = item.at("bytes").get<std::uint64_t>();

        if (address < previous || address >= ram_base + ram_bytes || !bytes ||
            bytes > 16 * 1024 * 1024 || bytes > ram_base + ram_bytes - address ||
            total + bytes > 16 * 1024 * 1024) {
            throw VoyemuError("QNX_CACHE_RAM", "ROM geometry rejected");
        }

        previous = address + bytes;
        total += bytes;
    }
}

void private_directory(const std::filesystem::path &path) {
    if (!path.is_absolute() || path.lexically_normal() != path ||
        std::filesystem::canonical(path) != path) {
        throw VoyemuError("QNX_CACHE_RAM", "ordinary absolute directory required");
    }

#ifndef _WIN32
    struct stat st{};

    if (::lstat(path.c_str(), &st) || !S_ISDIR(st.st_mode) || st.st_uid != ::geteuid() ||
        (st.st_mode & 0077)) {
        throw VoyemuError("QNX_CACHE_RAM", "private owned directory required");
    }
#endif
}

// Only v1 virt RAM is restored here; all other device/address-space shapes keep their loaders.
std::optional<std::map<std::string, std::string>> option_fields(std::string_view text,
                                                                std::string_view kind) {
    if (!text.starts_with(kind) || text.size() <= kind.size() || text[kind.size()] != ',') {
        return std::nullopt;
    }

    std::map<std::string, std::string> fields;
    std::size_t position = kind.size() + 1;

    while (position < text.size()) {
        auto end = text.find(',', position);

        if (end == std::string_view::npos) {
            end = text.size();
        }

        const auto field = text.substr(position, end - position);
        const auto equal = field.find('=');

        if (equal == std::string_view::npos || !equal || equal + 1 == field.size() ||
            !fields.emplace(field.substr(0, equal), field.substr(equal + 1)).second) {
            return std::nullopt;
        }

        position = end + 1;
    }

    return fields;
}

bool shared_virt_ram(const Json &launch) {
    const auto &arguments = launch.at("qemu").at("args");
    auto flag = [&](std::string_view name) {
        std::string value;
        bool found = false;

        for (std::size_t i = 0; i + 1 < arguments.size(); ++i) {
            if (arguments[i] == name) {
                if (found) {
                    return std::string{};
                }

                found = true;
                value = arguments[i + 1].get<std::string>();
            }
        }

        return value;
    };
    const auto machine = flag("-machine"), memory = flag("-m");
    const auto comma = machine.find(',');
    const auto kind = machine.substr(0, comma);

    if ((kind != "virt" && !kind.starts_with("virt-")) ||
        (memory != "8192" && memory != "8192M" && memory != "8G") || !launch.contains("ram")) {
        return false;
    }

    const auto machine_fields = option_fields(machine, kind);

    if (!machine_fields || !machine_fields->contains("memory-backend")) {
        return false;
    }

    bool shared_ram = false;

    for (std::size_t i = 0; i + 1 < arguments.size(); ++i) {
        if (arguments[i] == "-object") {
            const auto fields = option_fields(arguments[i + 1].get<std::string>(),
                                              "memory-backend-file");

            if (fields && fields->contains("id") &&
                fields->at("id") == machine_fields->at("memory-backend") &&
                fields->contains("size") &&
                (fields->at("size") == "8192M" || fields->at("size") == "8G" ||
                 fields->at("size") == "8589934592") &&
                fields->contains("mem-path") &&
                fields->at("mem-path") == launch.at("ram").get<std::string>() &&
                fields->contains("share") && fields->at("share") == "on") {
                shared_ram = true;
            }
        }
    }

    return shared_ram;
}

std::pair<std::size_t, std::uint64_t> omit_restored_ram_loaders(Json &launch,
                                                                Json *omitted_roms = nullptr) {
    if (!shared_virt_ram(launch)) {
        return {};
    }

    auto &arguments = launch["qemu"]["args"];
    Json retained = Json::array();
    std::size_t count = 0;
    std::uint64_t bytes = 0;

    for (std::size_t i = 0; i < arguments.size(); ++i) {
        bool omit = false;
        std::uint64_t size = 0;

        if (arguments[i] == "-device" && i + 1 < arguments.size()) {
            const auto fields = option_fields(arguments[i + 1].get<std::string>(), "loader");

            if (fields && fields->size() >= 3 && fields->size() <= 4 && fields->contains("file") &&
                fields->contains("addr") && fields->contains("force-raw") &&
                fields->at("force-raw") == "on" &&
                (fields->size() == 3 ||
                 (fields->contains("cpu-num") && fields->at("cpu-num") == "0"))) {
                const auto file = std::filesystem::path(fields->at("file"));
                const auto address_text = fields->at("addr");
                std::uint64_t address = 0;
                const auto begin = address_text.data() +
                                   std::min(std::size_t{2}, address_text.size());
                const auto parsed = std::from_chars(
                        begin, address_text.data() + address_text.size(), address, 16);
                const bool pinned = std::any_of(launch.at("pins").begin(), launch.at("pins").end(),
                                                [&](const Json &pin) {
                                                    return pin.at("path") == file.string();
                                                });

                if (address_text.starts_with("0x") && parsed.ec == std::errc{} &&
                    parsed.ptr == address_text.data() + address_text.size() && pinned &&
                    file.is_absolute() &&
                    std::filesystem::is_regular_file(std::filesystem::symlink_status(file))) {
                    size = std::filesystem::file_size(file);
                    omit = size && address >= 0x40000000ULL && address < 0x240000000ULL &&
                           size <= 0x240000000ULL - address;

                    if (omit && omitted_roms) {
                        omitted_roms->push_back(
                                {{"address", address}, {"bytes", size}, {"name", file.string()}});
                    }
                }
            }
        }

        if (omit) {
            ++count;
            bytes += size;
            ++i;
        } else {
            retained.push_back(arguments[i]);
        }
    }

    arguments = std::move(retained);

    return {count, bytes};
}

void verify(const std::filesystem::path &path, const Json &pin, std::uint64_t maximum,
            const std::string &name, const Json &previous, Json &files, std::size_t &reused) {
    owned_file(path);
    const auto bytes = pin.at("bytes").get<std::uint64_t>();
    const auto hash = pin.at("sha256").get<std::string>();

    if (!bytes || bytes > maximum || std::filesystem::file_size(path) != bytes ||
        !sha256_text(hash)) {
        throw VoyemuError("QNX_CACHE", "content extent/pin mismatch");
    }

    const auto identity = cache_file_identity(path);
    const Json entry = {{"pin", pin}, {"identity", identity}};

    if (previous.is_object() && previous.contains(name) && previous.at(name) == entry) {
        ++reused;
    } else if (sha256_file(path) != hash) {
        throw VoyemuError("QNX_CACHE", "content mismatch");
    }

    if (cache_file_identity(path) != identity) {
        throw VoyemuError("QNX_CACHE", "file changed during verification");
    }

    files[name] = entry;
}

bool files_match(const std::filesystem::path &directory, const Json &files) {
    if (!files.is_object()) {
        return false;
    }

    for (const auto &item : files.items()) {
        const auto relative = std::filesystem::path(item.key());

        if (relative.is_absolute() || relative.empty() || relative.lexically_normal() != relative ||
            item.key().find("..") != std::string::npos) {
            return false;
        }

        const auto path = directory / relative;
        owned_file(path);

        if (cache_file_identity(path) != item.value().at("identity")) {
            return false;
        }
    }

    return true;
}
} // namespace

Json qnx_checkpoint_key(const Json &cold, const std::string &profile) {
    auto prepared = cold;

    prepared.erase("quickboot");
    const auto renderer = prepared.at("graphics").at("executable").get<std::string>();
    prepared["graphics"].erase("executable");
    auto &pins = prepared.at("pins");
    pins.erase(std::remove_if(pins.begin(), pins.end(),
                              [&](const Json &pin) {
                                  return pin.at("path") == renderer;
                              }),
               pins.end());

    for (const auto &arg : prepared.at("qemu").at("args")) {
        if (arg == "-incoming") {
            throw VoyemuError("QNX_CACHE", "cold launch already has incoming");
        }
    }

    return {{"schema", 1},
            {"renderer_wire", 1},
            {"vehicle_maps", Json::array({qinggan09_map_sha256, qinggan11_map_sha256})},
            {"profile", profile},
            {"prepared", prepared}};
}

QnxCheckpoint runtime_qnx_checkpoint(const Json &cold, const std::string &profile) {
    QnxCheckpoint result;

    result.launch = cold;

    if (cold.contains("persistent_storage")) {
        result.reason = "persistent_storage_cold_only";

        return result;
    }

    if (cold.contains("interguest_network")) {
        validate_qnx_interguest_contract(cold.at("interguest_network"));
        result.reason = "network_v1_cold_only";

        return result;
    }

    if (!cold.contains("quickboot")) {
        return result;
    }

    try {
        result.directory = cold.at("quickboot").at("directory").get<std::string>();

        if (!result.directory.is_absolute() || std::filesystem::is_symlink(result.directory)) {
            throw VoyemuError("QNX_CACHE", "absolute ordinary directory required");
        }

        const auto file = result.directory / "manifest.json";

        if (!std::filesystem::exists(file)) {
            result.reason = "cache_missing";

            return result;
        }

        owned_file(file);
        const auto identity = cache_file_identity(file);
        const auto text = read_private_json_bytes(file, 128 * 1024);
        const auto manifest = Json::parse(text);
        result.manifest_sha256 = sha256_bytes(text);

        if (manifest.value("schema", 0) != 1 || manifest.value("state", std::string{}) != "ready" ||
            manifest.at("key") != qnx_checkpoint_key(cold, profile)) {
            result.reason = "incompatible_checkpoint";

            return result;
        }

        const auto rejected = result.directory / "rejected.json";

        if (std::filesystem::exists(rejected) &&
            read_private_json(rejected, 4096).value("manifest_sha256", std::string{}) ==
                    result.manifest_sha256) {
            result.reason = "checkpoint_quarantined";

            return result;
        }

        const auto state = manifest.at("state_sequence").get<std::uint64_t>(),
                   theme = manifest.at("theme_sequence").get<std::uint64_t>();

        if (state >= 0xfffffffeULL || theme >= 0xfffffffeULL ||
            manifest.at("wheel_sequence") != 0 || manifest.at("held_keys") != 0) {
            throw VoyemuError("QNX_CACHE", "unsafe revision or held key state");
        }

        const auto addresses = manifest.at("producers");

        if (!addresses.is_array() || addresses.empty() || addresses.size() > 8) {
            throw VoyemuError("QNX_CACHE", "producer count");
        }

        std::set<std::uint64_t> unique;

        for (const auto &item : addresses) {
            const auto address = item.get<std::uint64_t>();

            if (address < ram_base || address > ram_base + ram_bytes - 4096 || address % 4096 ||
                !unique.insert(address).second) {
                throw VoyemuError("QNX_CACHE", "producer geometry");
            }

            result.producers.push_back(address);
        }

        Json files = Json::object();
        const auto retain = [&](const std::string &name, const Json &description) {
            const auto path = result.directory / name;
            owned_file(path);
            files[name] = {{"pin", description}, {"identity", cache_file_identity(path)}};
        };
        retain("vmstate.bin", manifest.at("vm"));
        const auto gpu = manifest.at("gpu");

        if (!gpu.is_object() || gpu.size() != unique.size()) {
            throw VoyemuError("QNX_CACHE", "GPU producer identity");
        }

        for (const auto address : result.producers) {
            std::ostringstream name;
            name << std::hex << address << ".gpu";
            retain("gpu/" + name.str(), gpu.at(name.str()));
        }

        if (manifest.contains("memory")) {
            const auto &memory = manifest.at("memory");

            if (!memory.is_object() || memory.size() != 3 ||
                memory.value("format", std::string{}) != "shared-file-cpr-v1" ||
                cold.value("guest_arch", std::string{}) != "aarch64" || !shared_virt_ram(cold)) {
                throw VoyemuError("QNX_CACHE_RAM", "unsupported memory configuration");
            }

#ifdef __APPLE__
            private_directory(result.directory);
            struct statfs storage{};

            if (::statfs(result.directory.c_str(), &storage) ||
                std::string_view(storage.f_fstypename) != "apfs") {
                throw VoyemuError("QNX_CACHE_RAM", "APFS storage required");
            }

            validate_boot_roms(memory.at("boot_roms"));
            result.boot_roms = memory.at("boot_roms");

            if (memory.at("ram").at("bytes") != ram_bytes) {
                throw VoyemuError("QNX_CACHE_RAM", "exact 8GiB source required");
            }

            retain("ram.bin", memory.at("ram"));
            result.external_ram = true;
#else
            throw VoyemuError("QNX_CACHE_RAM", "APFS restore requires macOS");
#endif
        }

        result.launch["qemu"]["args"].push_back("-incoming");
        result.launch["qemu"]["args"].push_back(
                result.external_ram ? "defer"
                                    : "file:" + (result.directory / "vmstate.bin").string());
        result.launch["graphics"]["replay"] = (result.directory / "gpu").string();

        if (cache_file_identity(file) != identity) {
            throw VoyemuError("QNX_CACHE", "configuration changed");
        }

        const auto omitted = omit_restored_ram_loaders(result.launch);
        result.omitted_loaders = omitted.first;
        result.omitted_loader_bytes = omitted.second;
        result.verified_files = {{"schema", 1},
                                 {"manifest_sha256", result.manifest_sha256},
                                 {"manifest_identity", identity},
                                 {"files", files}};
        result.verification = "configuration";
        result.state_sequence = static_cast<std::uint32_t>(state);
        result.theme_sequence = static_cast<std::uint32_t>(theme);
        result.resume = true;
        result.reason = "prepared_checkpoint";

        return result;
    } catch (const std::exception &) {
        result = QnxCheckpoint{};
        result.launch = cold;
        result.reason = "cache_rejected";

        return result;
    }
}

QnxCheckpoint inspect_qnx_checkpoint(const Json &cold, const std::string &profile,
                                     bool reuse_verified_files) {
    QnxCheckpoint result;

    result.launch = cold;

    if (cold.contains("persistent_storage")) {
        result.reason = "persistent_storage_cold_only";

        return result;
    }

    if (cold.contains("interguest_network")) {
        validate_qnx_interguest_contract(cold.at("interguest_network"));
        result.reason = "network_v1_cold_only";

        return result;
    }

    if (!cold.contains("quickboot")) {
        return result;
    }

    try {
        result.directory = cold.at("quickboot").at("directory").get<std::string>();

        if (!result.directory.is_absolute() || std::filesystem::is_symlink(result.directory)) {
            throw VoyemuError("QNX_CACHE", "absolute ordinary directory required");
        }

        const auto file = result.directory / "manifest.json";

        if (!std::filesystem::exists(file)) {
            result.reason = "cache_missing";

            return result;
        }

        owned_file(file);
        const auto manifest_identity = cache_file_identity(file);
        const auto manifest = read_private_json(file, 128 * 1024);
        result.manifest_sha256 = sha256_file(file);

        if (manifest.value("schema", 0) != 1 || manifest.value("state", std::string{}) != "ready" ||
            manifest.at("key") != qnx_checkpoint_key(cold, profile)) {
            result.reason = "incompatible_checkpoint";

            return result;
        }

        const auto rejected = result.directory / "rejected.json";

        if (std::filesystem::exists(rejected) &&
            read_private_json(rejected, 4096).value("manifest_sha256", std::string{}) ==
                    result.manifest_sha256) {
            result.reason = "checkpoint_quarantined";

            return result;
        }

        const auto state = manifest.at("state_sequence").get<std::uint64_t>(),
                   theme = manifest.at("theme_sequence").get<std::uint64_t>();

        if (state >= 0xfffffffeULL || theme >= 0xfffffffeULL ||
            manifest.at("wheel_sequence") != 0 || manifest.at("held_keys") != 0) {
            throw VoyemuError("QNX_CACHE", "unsafe revision or held key state");
        }

        const auto addresses = manifest.at("producers");

        if (!addresses.is_array() || addresses.empty() || addresses.size() > 8) {
            throw VoyemuError("QNX_CACHE", "producer count");
        }

        std::set<std::uint64_t> unique;

        for (const auto &value : addresses) {
            auto address = value.get<std::uint64_t>();

            if (address < 0x40000000ULL || address > 0x240000000ULL - 4096 || address % 4096 ||
                !unique.insert(address).second) {
                throw VoyemuError("QNX_CACHE", "producer geometry");
            }

            result.producers.push_back(address);
        }

        Json previous = Json::object(), files = Json::object();
        std::size_t reused = 0;

        if (reuse_verified_files) {
            try {
                const auto receipt_path = result.directory / verification_name;

                if (std::filesystem::exists(receipt_path) ||
                    std::filesystem::is_symlink(receipt_path)) {
                    owned_file(receipt_path);
                    const auto receipt = read_private_json(receipt_path, 32768);

                    if (receipt.value("schema", 0) == 1 &&
                        receipt.value("manifest_sha256", std::string{}) == result.manifest_sha256 &&
                        receipt.at("files").is_object()) {
                        previous = receipt.at("files");
                    }
                }
            } catch (const std::exception &) { /* Ignored: invalid optional receipt falls back to
                                                  complete SHA verification. */
            }
        }

        const auto vm = result.directory / "vmstate.bin";
        verify(vm, manifest.at("vm"), 16ULL * 1024 * 1024 * 1024, "vmstate.bin", previous, files,
               reused);
        std::ifstream input(vm, std::ios::binary);
        std::array<unsigned char, 8> header{};
        input.read(reinterpret_cast<char *>(header.data()), 8);

        if (header != std::array<unsigned char, 8>{0x51, 0x45, 0x56, 0x4d, 0, 0, 0, 3}) {
            throw VoyemuError("QNX_CACHE", "migration format");
        }

        const auto gpu = manifest.at("gpu");

        if (!gpu.is_object() || gpu.size() != unique.size()) {
            throw VoyemuError("QNX_CACHE", "GPU producer identity");
        }

        for (auto address : result.producers) {
            std::ostringstream name;
            name << std::hex << address << ".gpu";
            const auto path = result.directory / "gpu" / name.str();
            verify(path, gpu.at(name.str()), 2ULL * 1024 * 1024 * 1024, "gpu/" + name.str(),
                   previous, files, reused);
            std::ifstream journal(path, std::ios::binary);
            std::array<std::uint64_t, 3> words{};
            journal.read(reinterpret_cast<char *>(words.data()), 24);

            if (!journal || words != std::array<std::uint64_t, 3>{0x3130305550475156ULL, address,
                                                                  8ULL * 1024 * 1024 * 1024}) {
                throw VoyemuError("QNX_CACHE", "GPU format or owner");
            }
        }

        if (manifest.contains("memory")) {
            const auto &memory = manifest.at("memory");

            if (!memory.is_object() || memory.size() != 3 ||
                memory.value("format", std::string{}) != "shared-file-cpr-v1" ||
                !memory.contains("ram") || !memory.contains("boot_roms") ||
                cold.value("guest_arch", std::string{}) != "aarch64" || !shared_virt_ram(cold)) {
                throw VoyemuError("QNX_CACHE_RAM", "unsupported memory format or machine");
            }

#ifndef __APPLE__
            throw VoyemuError("QNX_CACHE_RAM", "APFS restore requires macOS");
#else
            private_directory(result.directory);
            struct statfs storage{};

            if (::statfs(result.directory.c_str(), &storage) ||
                std::string_view(storage.f_fstypename) != "apfs") {
                throw VoyemuError("QNX_CACHE_RAM", "APFS storage required");
            }

            validate_boot_roms(memory.at("boot_roms"));
            result.boot_roms = memory.at("boot_roms");

            if (memory.at("ram").at("bytes") != ram_bytes) {
                throw VoyemuError("QNX_CACHE_RAM", "exact 8GiB source required");
            }

            verify(result.directory / "ram.bin", memory.at("ram"), ram_bytes, "ram.bin", previous,
                   files, reused);
            result.external_ram = true;
#endif
        }

        result.launch["qemu"]["args"].push_back("-incoming");
        result.launch["qemu"]["args"].push_back(result.external_ram ? "defer"
                                                                    : "file:" + vm.string());
        result.launch["graphics"]["replay"] = (result.directory / "gpu").string();

        if (cache_file_identity(file) != manifest_identity ||
            !files_match(result.directory, files)) {
            throw VoyemuError("QNX_CACHE", "verification epoch changed");
        }

        const auto omitted = omit_restored_ram_loaders(result.launch);
        result.omitted_loaders = omitted.first;
        result.omitted_loader_bytes = omitted.second;
        result.verified_files = {{"schema", 1},
                                 {"manifest_sha256", result.manifest_sha256},
                                 {"manifest_identity", manifest_identity},
                                 {"files", files}};
        result.verification = reused == files.size() ? "cached_identity"
                              : reused               ? "mixed"
                                                     : "full_sha256";
        result.state_sequence = static_cast<std::uint32_t>(state);
        result.theme_sequence = static_cast<std::uint32_t>(theme);
        result.resume = true;
        result.reason = "validated_checkpoint";

        return result;
    } catch (const std::exception &) {
        result.resume = false;
        result.external_ram = false;
        result.boot_roms = Json{};
        result.launch = cold;
        result.producers.clear();
        result.omitted_loaders = 0;
        result.omitted_loader_bytes = 0;
        result.reason = "cache_rejected";

        return result;
    }
}

bool remember_qnx_checkpoint_verification(const QnxCheckpoint &saved) {
    if (!saved.resume || (saved.verification != "full_sha256" && saved.verification != "mixed") ||
        saved.verified_files.value("schema", 0) != 1 ||
        saved.verified_files.at("manifest_sha256") != saved.manifest_sha256) {
        throw VoyemuError("QNX_CACHE", "fresh content verification required for receipt");
    }

    try {
        const auto manifest = saved.directory / "manifest.json";
        owned_file(manifest);

        if (cache_file_identity(manifest) != saved.verified_files.at("manifest_identity") ||
            sha256_file(manifest) != saved.manifest_sha256 ||
            !files_match(saved.directory, saved.verified_files.at("files"))) {
            return false;
        }
    } catch (const std::exception &) {
        return false;
    }

    const auto receipt = saved.directory / verification_name;

    if (std::filesystem::exists(receipt) || std::filesystem::is_symlink(receipt)) {
        owned_file(receipt);
    }

    write_private_json(receipt, saved.verified_files);

    return true;
}

Json qnx_checkpoint_boot_roms(std::string_view response, const Json &cold) {
    if (response.empty() || response.size() > 16384) {
        throw VoyemuError("QNX_CACHE_RAM", "bounded ROM observation required");
    }

    Json omitted = Json::array();

    if (!cold.is_null()) {
        auto warm = cold;
        omit_restored_ram_loaders(warm, &omitted);
    }

    const std::regex record(
            R"rom(^addr=([0-9a-fA-F]+) size=0x([0-9a-fA-F]+) mem=ram name="([^"\r\n]+)"$)rom");
    std::istringstream input{std::string(response)};
    std::string line;
    Json result = Json::array();

    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }

        if (line.starts_with("/rom@")) {
            continue;
        }

        std::smatch match;

        if (!std::regex_match(line, match, record)) {
            throw VoyemuError("QNX_CACHE_RAM", "unknown ROM observation");
        }

        auto hexadecimal = [](const std::string &text) {
            std::uint64_t value = 0;
            auto parsed = std::from_chars(text.data(), text.data() + text.size(), value, 16);

            if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size()) {
                throw VoyemuError("QNX_CACHE_RAM", "invalid ROM address");
            }

            return value;
        };
        const auto address = hexadecimal(match[1]), bytes = hexadecimal(match[2]);
        const auto name = match[3].str();
        const auto loader = std::find_if(omitted.begin(), omitted.end(), [&](const Json &item) {
            return item.at("address") == address && item.at("bytes") == bytes &&
                   item.at("name") == name;
        });

        if (loader != omitted.end()) {
            omitted.erase(loader);
            continue;
        }

        result.push_back({{"address", address}, {"bytes", bytes}});
    }

    if (!omitted.empty()) {
        throw VoyemuError("QNX_CACHE_RAM", "expected raw loader ROM not observed");
    }

    std::sort(result.begin(), result.end(), [](const Json &a, const Json &b) {
        return a.at("address").get<std::uint64_t>() < b.at("address").get<std::uint64_t>();
    });
    validate_boot_roms(result);

    return result;
}

namespace {
void checkpoint_ram_epoch(const QnxCheckpoint &saved) {
    if (!saved.resume || !saved.external_ram ||
        !saved.verified_files.at("files").contains("ram.bin") ||
        cache_file_identity(saved.directory / "manifest.json") !=
                saved.verified_files.at("manifest_identity") ||
        !files_match(saved.directory, saved.verified_files.at("files"))) {
        throw VoyemuError("QNX_CACHE_RAM", "verified checkpoint changed");
    }
}
} // namespace

Json clone_qnx_checkpoint_ram(const QnxCheckpoint &saved, const std::filesystem::path &target) {
#ifndef __APPLE__
    (void)saved;
    (void)target;
    throw VoyemuError("QNX_CACHE_RAM", "APFS restore requires macOS");
#else
    checkpoint_ram_epoch(saved);
    private_directory(saved.directory);
    private_directory(target.parent_path());

    if (target.filename() != "qnx-ram.bin" || std::filesystem::exists(target) ||
        std::filesystem::is_symlink(target)) {
        throw VoyemuError("QNX_CACHE_RAM", "exact fresh target required");
    }

    if (::clonefile((saved.directory / "ram.bin").c_str(), target.c_str(), CLONE_NOFOLLOW_ANY)) {
        throw VoyemuError("QNX_CACHE_RAM", "APFS clone failed");
    }

    owned_file(target);

    return cache_file_identity(target);
#endif
}

void restore_qnx_checkpoint_boot_roms(const QnxCheckpoint &saved,
                                      const std::filesystem::path &target,
                                      std::string_view actual_roms) {
#ifndef __APPLE__
    (void)saved;
    (void)target;
    (void)actual_roms;
    throw VoyemuError("QNX_CACHE_RAM", "APFS restore requires macOS");
#else
    checkpoint_ram_epoch(saved);
    validate_boot_roms(saved.boot_roms);

    if (qnx_checkpoint_boot_roms(actual_roms) != saved.boot_roms) {
        throw VoyemuError("QNX_CACHE_RAM", "actual ROM layout changed");
    }

    private_directory(target.parent_path());
    owned_file(target);

    if (target.filename() != "qnx-ram.bin" || std::filesystem::file_size(target) != ram_bytes) {
        throw VoyemuError("QNX_CACHE_RAM", "exact owned RAM required");
    }

    struct File {
        int fd;

        ~File() {
            if (fd >= 0) {
                ::close(fd);
            }
        }
    };
    File source{::open((saved.directory / "ram.bin").c_str(), O_RDONLY | O_NOFOLLOW | O_CLOEXEC)},
            destination{::open(target.c_str(), O_RDWR | O_NOFOLLOW | O_CLOEXEC)};
    struct stat src{}, dst{};

    if (source.fd < 0 || destination.fd < 0 || ::fstat(source.fd, &src) ||
        ::fstat(destination.fd, &dst) ||
        src.st_ino != saved.verified_files.at("files")
                              .at("ram.bin")
                              .at("identity")
                              .at("inode")
                              .get<ino_t>() ||
        dst.st_ino != cache_file_identity(target).at("inode").get<ino_t>()) {
        throw VoyemuError("QNX_CACHE_RAM", "opened RAM identity changed");
    }

    for (const auto &item : saved.boot_roms) {
        const auto bytes = item.at("bytes").get<std::size_t>();
        const auto offset = item.at("address").get<std::uint64_t>() - ram_base;
        std::vector<char> original(bytes), readback(bytes);
        auto read = [&](int fd, std::vector<char> &data) {
            std::size_t done = 0;

            while (done < data.size()) {
                const auto count = ::pread(fd, data.data() + done, data.size() - done,
                                           offset + done);

                if (count < 0 && errno == EINTR) {
                    continue;
                }

                if (count <= 0) {
                    throw VoyemuError("QNX_CACHE_RAM", "incomplete ROM read");
                }

                done += count;
            }
        };
        read(source.fd, original);
        std::size_t done = 0;

        while (done < bytes) {
            const auto count = ::pwrite(destination.fd, original.data() + done, bytes - done,
                                        offset + done);

            if (count < 0 && errno == EINTR) {
                continue;
            }

            if (count <= 0) {
                throw VoyemuError("QNX_CACHE_RAM", "incomplete ROM write");
            }

            done += count;
        }

        read(destination.fd, readback);

        if (readback != original) {
            throw VoyemuError("QNX_CACHE_RAM", "ROM readback mismatch");
        }
    }

    if (::fsync(destination.fd)) {
        throw VoyemuError("QNX_CACHE_RAM", "ROM synchronization failed");
    }

    checkpoint_ram_epoch(saved);
    struct stat current{};

    if (::lstat(target.c_str(), &current) || current.st_ino != dst.st_ino ||
        current.st_dev != dst.st_dev) {
        throw VoyemuError("QNX_CACHE_RAM", "repaired target changed");
    }
#endif
}

void reject_qnx_checkpoint(const QnxCheckpoint &saved, std::string_view reason,
                           bool restore_ready) {
    if (restore_ready) {
        return;
    }

    if (!saved.resume || saved.manifest_sha256.empty() || reason.size() > 64) {
        throw VoyemuError("QNX_CACHE", "invalid rejection identity");
    }

    write_private_json(
            saved.directory / "rejected.json",
            {{"schema", 1}, {"manifest_sha256", saved.manifest_sha256}, {"reason", reason}});
}

bool qnx_checkpoint_migration_ready(const Json &migration, const Json &status) {
    if (migration.value("status", std::string{}) == "failed") {
        throw VoyemuError("QNX_CHECKPOINT", "incoming failed");
    }

    const auto state = status.value("status", std::string{});

    return migration.value("status", std::string{}) == "completed" &&
           ((state == "paused" && !status.value("running", true)) ||
            (state == "running" && status.value("running", false)));
}
} // namespace voyemu
