#pragma once
#include "cli_messages.hpp"
#include "model.hpp"
#include <sstream>
#include <stdexcept>
#include <string_view>
namespace voyemu {
inline std::string human_cli_error(std::string_view message) {
    return "Voyemu: " + std::string(message) + "\n";
}

// Errors outside the CLI catalog keep their English "CODE: message".
inline std::string human_cli_error(const std::exception &error, cli::Language language) {
    const auto *localized = dynamic_cast<const cli::LocalizedError *>(&error);

    return human_cli_error(localized ? localized->render(language) : std::string(error.what()));
}

// Pads to a terminal column width: CJK ideographs and fullwidth forms take two columns.
inline std::string pad_columns(std::string_view text, std::size_t width) {
    std::size_t columns = 0;

    for (std::size_t i = 0; i < text.size();) {
        const auto lead = static_cast<unsigned char>(text[i]);
        const std::size_t length = lead < 0x80 ? 1 : lead < 0xE0 ? 2 : lead < 0xF0 ? 3 : 4;
        char32_t code = length == 1 ? lead : lead & (0x7F >> length);

        for (std::size_t j = 1; j < length && i + j < text.size(); ++j) {
            code = (code << 6) | (static_cast<unsigned char>(text[i + j]) & 0x3F);
        }

        columns += (code >= 0x1100 && code <= 0x115F) || (code >= 0x2E80 && code <= 0xA4CF) ||
                                   (code >= 0xAC00 && code <= 0xD7A3) ||
                                   (code >= 0xF900 && code <= 0xFAFF) ||
                                   (code >= 0xFE30 && code <= 0xFE4F) ||
                                   (code >= 0xFF00 && code <= 0xFF60) ||
                                   (code >= 0xFFE0 && code <= 0xFFE6) || code >= 0x20000
                           ? 2
                           : 1;
        i += length;
    }

    return std::string(text) + std::string(columns < width ? width - columns : 1, ' ');
}

inline std::string human_cli_output(std::string_view command, const Json &result,
                                    std::string_view profile,
                                    const std::filesystem::path &machine_path = {},
                                    bool verbose = false,
                                    cli::Language language = cli::Language::english) {
    const auto text = [&](std::string_view key, const cli::MessageArguments &arguments = {}) {
        return cli::message(language, key, arguments);
    };
    // deps, check and build name a QNX engine that this host cannot install.
    const auto qnx_host_line = [&]() {
        return result.is_object() && result.contains("qnx_host") &&
                               result.at("qnx_host").value("status", std::string{}) == "unavailable"
                       ? text("output.qnx_host_unavailable") + "\n"
                       : std::string{};
    };
    auto label = machine_path.empty() ? std::string(profile) : machine_path.string();

    if (result.is_object() && result.contains("machine_seed") &&
        result.at("machine_seed").is_string()) {
        label = result.at("machine_seed").get<std::string>();
    }

    if (result.is_object() && result.contains("ok") && result.at("ok") == false) {
        const auto failed = text("output.operation_failed");
        const auto error = result.value("error", Json(failed));

        return human_cli_error(error.is_string() ? error.get<std::string>()
                                                 : error.value("message", failed));
    }

    if (command == "start" && !verbose) {
        const auto machine = machine_path.empty() ? label : machine_path.string();
        // A new session reports its subsystems; the status of a running one does not.
        const bool cluster = result.value("with_cluster", false),
                   multimedia = result.value("with_multimedia", false);
        const auto first = !cluster && !multimedia
                                   ? text("output.already_running", {{"machine", machine}})
                           : !cluster    ? text("output.session_started_multimedia")
                           : !multimedia ? text("output.session_started_cluster")
                                         : text("output.session_started_both");

        return first + "\n" + text("output.stop_hint", {{"machine", machine}}) + "\n";
    }

    std::ostringstream out;

    if (command == "list") {
        out << text("output.profiles") << '\n';

        for (const auto &name : result) {
            out << "  " << name.get<std::string>() << '\n';
        }

        return out.str();
    }

    if (command == "start" || command == "restart") {
        const auto state = [&](const char *field) {
            return text(result.at(field).get<bool>() ? "output.on" : "output.off");
        };
        out << text("output.session_active", {{"machine", label}}) << '\n';

        if (result.contains("with_cluster")) {
            out << text("output.cluster", {{"state", state("with_cluster")}}) << '\n';
        }

        if (result.contains("with_multimedia")) {
            out << text("output.multimedia", {{"state", state("with_multimedia")}}) << '\n';
        }

        return out.str();
    }

    if (command == "status") {
        const auto deliveries = result.value("deliveries", Json::object());
        auto state = [&](const char *role) {
            const auto delivery = deliveries.value(role, Json::object());

            if (!delivery.contains("status")) {
                return text("output.not_started");
            }

            const auto value = delivery.value("status", std::string{});

            if (value == "guest_acknowledged") {
                return text("output.ready");
            }

            if (value == "not_started" || value == "stopped" || value == "host_terminal") {
                return text("output.off");
            }

            return value;
        };
        out << text("output.machine", {{"machine", label}}) << '\n'
            << pad_columns(text("output.column_state"), 14)
            << pad_columns(text("output.column_multimedia"), 22) << text("output.column_cluster")
            << '\n'
            << pad_columns(
                       text(result.value("stopped", false) ? "output.stopped" : "output.running"),
                       14)
            << pad_columns(state("android"), 22) << state("qnx") << '\n';
        const auto grpc =
                deliveries.value("qnx", Json::object()).value("frame_grpc", Json::object());

        if (!result.value("stopped", false) && grpc.value("status", std::string{}) == "listening") {
            out << text("output.frame_grpc", {{"path", grpc.at("discovery").get<std::string>()}})
                << '\n';
        }

        return out.str();
    }

    if (command == "stop") {
        return text("output.session_stopped", {{"machine", label}}) + "\n";
    }

    if (command == "patch") {
        return text("output.vehicle_updated", {{"machine", label}}) + "\n";
    }

    if (command == "check") {
        const auto launch = [&](const char *field) {
            return text(result.at(field).get<bool>() ? "output.available" : "output.missing");
        };
        out << text("output.profile", {{"profile", std::string(profile)}}) << '\n'
            << text("output.installation", {{"path", result.at("root").get<std::string>()}}) << '\n'
            << text("output.multimedia_launch", {{"state", launch("android_launch_spec")}}) << '\n'
            << text("output.cluster_launch", {{"state", launch("cluster_launch_spec")}}) << '\n';
        out << qnx_host_line();

        return out.str();
    }

    if (command == "fetch") {
        return text("output.fetch", {{"profile", std::string(profile)}}) + "\n";
    }

    if (command == "deps") {
        out << text("output.deps") << '\n';

        if (result.contains("preparation_tools_ready") &&
            !result.at("preparation_tools_ready").get<bool>()) {
            out << text("output.deps_java") << '\n';
        }

        out << qnx_host_line();

        return out.str();
    }

    if (command == "build") {
        out << text(result.value("profile_ready", false)   ? "output.machine_prepared"
                    : result.value("android_ready", false) ? "output.multimedia_prepared"
                                                           : "output.build_incomplete",
                    {{"machine", label}})
            << '\n';

        if (result.contains("runtime")) {
            out << text("output.runtime", {{"path", result.at("runtime").get<std::string>()}})
                << '\n';
        }

        out << qnx_host_line();

        return out.str();
    }

    throw std::logic_error("CLI_OUTPUT: unsupported public command");
}
} // namespace voyemu
