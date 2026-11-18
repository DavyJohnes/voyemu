#include "toolchain_manifest.hpp"
#include "error.hpp"
#include "os/platform.hpp"
#include <iterator>
#include <stdexcept>
namespace voyemu {
namespace {
#include "toolchain_catalog.inc"

// The sdk.runtimeFilePins entry of host with key, or null.
const Json *runtime_file_entry(std::string_view host, const char *key, const std::string &file) {
    const auto slash = host.find('/');
    const auto &pins = embedded_toolchain_manifest().at("sdk").at("runtimeFilePins");

    if (slash == std::string_view::npos) {
        return nullptr;
    }

    const std::string system(host.substr(0, slash)), architecture(host.substr(slash + 1));

    if (!pins.contains(system) || !pins.at(system).contains(architecture) ||
        !pins.at(system).at(architecture).contains(key) ||
        !pins.at(system).at(architecture).at(key).contains(file)) {
        return nullptr;
    }

    return &pins.at(system).at(architecture).at(key).at(file);
}
} // namespace

const Json &embedded_toolchain_manifest() {
    static const Json result = Json::parse(std::begin(toolchain_json), std::end(toolchain_json));

    return result;
}

std::string native_runtime_file_pin(std::string_view host, const std::string &file) {
    const auto *pin = runtime_file_entry(host, "files", file);

    if (!pin) {
        throw VoyemuError("DEPS_HOST", "no runtime file pin for " + std::string(host) + " " + file);
    }

    return pin->get<std::string>();
}

std::uint64_t native_runtime_file_bytes(std::string_view host, const std::string &file) {
    const auto *bytes = runtime_file_entry(host, "fileBytes", file);

    if (!bytes) {
        throw VoyemuError("DEPS_HOST",
                          "no runtime file size for " + std::string(host) + " " + file);
    }

    return bytes->get<std::uint64_t>();
}

const Json &native_ndk_archive() {
    if (os::dependency_system.empty()) {
        throw VoyemuError("DEPS_HOST", "macOS, Windows x64 or Linux x64 required");
    }

    return embedded_toolchain_manifest()
            .at("sdk")
            .at("archives")
            .at(std::string(os::dependency_system))
            .at("ndk");
}
} // namespace voyemu
