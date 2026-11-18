#include "error.hpp"
#include "session_internal.hpp"

namespace voyemu {
using namespace session_detail;
namespace session_detail {
int cluster_ui_timeout(const Json &operation) {
    if (!operation.contains("timeout_ms")) {
        return 250;
    }

    const auto &value = operation.at("timeout_ms");

    if (!value.is_number_integer() || value < 1 || value > 30000) {
        throw VoyemuError("TEST_TIMEOUT", "invalid cluster observation timeout");
    }

    return value.get<int>();
}

std::size_t response_limit(const Json &operation) {
    const auto name = operation.value("op", std::string{});

    if (name == "test_cluster_ui") {
        return qnx_kanzi_ui_response_bytes;
    }

    if (name == "ui_observer_exchange") {
        return observer_response_bytes + 4096;
    }

    if (name == "test_output_logs" && operation.value("source", std::string{}) == "native") {
        return 256 * 1024;
    }

    return maximum_message;
}

int cluster_remaining(ClusterDeadline deadline) {
    const auto remaining = std::chrono::ceil<std::chrono::milliseconds>(
                                   deadline - std::chrono::steady_clock::now())
                                   .count();

    if (remaining <= 0) {
        throw VoyemuError("CLUSTER_UI_RESOURCE_LIMIT", "response transport deadline");
    }

    return static_cast<int>(std::min<std::int64_t>(remaining, 30000));
}

std::string bounded_message(const Json &value, std::size_t limit) {
    auto text = value.dump() + "\n";

    if (text.size() > limit) {
        throw VoyemuError("IPC_EXTENT", "message too large");
    }

    return text;
}

Json ControlTimeout::parse(const std::string &bytes) const {
    if (cluster) {
        (void)remaining();
    }

    auto result = Json::parse(bytes);

    if (cluster) {
        (void)remaining();
    }

    return result;
}

std::string replace_all(std::string value, const std::string &from, const std::string &to) {
    std::size_t pos = 0;

    while ((pos = value.find(from, pos)) != std::string::npos) {
        value.replace(pos, from.size(), to);
        pos += to.size();
    }

    return value;
}

void validate_pins(const Json &specification, bool machine_start) {
    if (machine_start) {
        if (specification.value("schema", 0) != 1) {
            throw VoyemuError("LAUNCH_SCHEMA", "prepared machine launch required");
        }

        const auto require = [](const Json &object, const char *key) {
            if (!object.contains(key)) {
                return;
            }

            const std::filesystem::path file = object.at(key).get<std::string>();

            if (!file.is_absolute() || !std::filesystem::is_regular_file(file)) {
                throw VoyemuError("LAUNCH_FILE", std::string("executable unavailable: ") + key);
            }
        };
        require(specification, "executable");

        if (specification.contains("qemu")) {
            require(specification.at("qemu"), "executable");
        }

        if (specification.contains("graphics")) {
            require(specification.at("graphics"), "executable");
        }

        return;
    }

    if (specification.value("schema", 0) != 1 || !specification.contains("pins") ||
        !specification["pins"].is_array() || specification["pins"].empty()) {
        throw VoyemuError("LAUNCH_SCHEMA", "pinned specification required");
    }

    std::set<std::string> pinned;

    for (const auto &pin : specification["pins"]) {
        auto path = std::filesystem::path(pin.at("path").get<std::string>());
        auto expected = pin.at("sha256").get<std::string>();

        if (!path.is_absolute() || expected.size() != 64 || sha256_file(path) != expected) {
            throw VoyemuError("LAUNCH_PIN", "immutable input mismatch");
        }

        pinned.insert(path.string());
    }

    auto require = [&](const Json &object, const char *key) {
        if (object.contains(key) && !pinned.contains(object.at(key).get<std::string>())) {
            throw VoyemuError("LAUNCH_PIN",
                              std::string("executable/library is not pinned: ") + key);
        }
    };
    require(specification, "executable");

    if (specification.contains("qemu")) {
        require(specification["qemu"], "executable");
    }

    if (specification.contains("graphics")) {
        require(specification["graphics"], "executable");
        auto angle =
                std::filesystem::path(specification["graphics"].at("angle").get<std::string>());

        for (auto library : angle_libraries) {
            if (!pinned.contains((angle / library).string())) {
                throw VoyemuError("LAUNCH_PIN", "ANGLE library is not pinned");
            }
        }
    }
}

ProcessSpec launch_spec(const Json &specification, const std::filesystem::path &directory,
                        const std::string &token) {
    ProcessSpec process;

    process.executable = specification.at("executable").get<std::string>();
    process.directory = directory;
    auto replace = [&](std::string value) {
        return expand_session_path(std::move(value), directory, token);
    };

    for (const auto &argument : specification.value("args", Json::array())) {
        process.arguments.push_back(replace(argument.get<std::string>()));
    }

    auto environment = specification.value("env", Json::object());

    for (const auto &[name, value] : environment.items()) {
        process.environment[name] = replace(value.get<std::string>());
    }

    process.output = directory / "process.log";

    return process;
}

int route_index(const std::string &id) {
    for (const auto &route : physical_routes) {
        if (id == route.id) {
            return route.index;
        }
    }

    throw VoyemuError("WHEEL_ID", "unknown physical button");
}

void owner_valid(const std::string &owner) {
    if (owner.empty() || owner.size() > 64 ||
        !std::all_of(owner.begin(), owner.end(), [](unsigned char c) {
            return std::isalnum(c) || c == '_' || c == '-';
        })) {
        throw VoyemuError("WHEEL_OWNER", "bounded token required");
    }
}

std::uint64_t u64(const Json &value) {
    if (value.is_string()) {
        return std::stoull(value.get<std::string>(), nullptr, 0);
    }

    return value.get<std::uint64_t>();
}
} // namespace session_detail

std::string expand_session_path(std::string value, const std::filesystem::path &directory,
                                const std::string &token) {
    value = replace_all(replace_all(std::move(value), "${SESSION}", directory.string()), "${TOKEN}",
                        token);

    if (value.find("${RUNTIME}") != std::string::npos) {
        value = replace_all(std::move(value), "${RUNTIME}", session_runtime_root().string());
    }

    return value;
}

int request_timeout_limit_ms(std::string_view op) {
    if (op == "android_reboot" || op == "machine_restart") {
        return 180000;
    }

    if (op == "stop" || op == "cluster_power" || op == "cluster_restart") {
        return 90000 + static_cast<int>(qnx_cluster_close_extra().count());
    }

    return op == "media_power" || op == "test_media_power" || op == "media_restart" ? 90000 : 30000;
}

Json request(const std::filesystem::path &endpoint, const Json &operation, int timeout_ms) {
    if (timeout_ms < 1 ||
        timeout_ms > request_timeout_limit_ms(operation.value("op", std::string{}))) {
        throw VoyemuError("IPC_BOUNDS", "timeout out of range");
    }

    const ControlTimeout timeout{
            operation.value("op", std::string{}) == "test_cluster_ui",
            std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms), timeout_ms};

    return control_exchange(endpoint, bounded_message(operation), operation, timeout);
}
} // namespace voyemu
