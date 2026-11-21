#pragma once
#include "cli_messages.hpp"
#include "model.hpp"
#include <fstream>
namespace voyemu {
// Expose only a structured error code. The log may contain private launch data.
inline std::string supervisor_failure_code(std::string_view tail) {
    std::string code;
    std::size_t offset = 0;

    while (offset < tail.size()) {
        const auto end = tail.find('\n', offset);
        const auto line = tail.substr(offset, end == std::string_view::npos ? tail.size() - offset
                                                                            : end - offset);
        offset = end == std::string_view::npos ? tail.size() : end + 1;
        const auto value = Json::parse(std::string(line), nullptr, false);

        if (value.is_discarded() || !value.is_object() || value.value("ok", true) ||
            !value.contains("error") || !value.at("error").is_string()) {
            continue;
        }

        const auto error = value.at("error").get<std::string>();

        if (const auto found = message_code(error); !found.empty()) {
            code = found;
        }
    }

    return code;
}

// The structured error code at the end of a supervisor log; empty when the log names none.
inline std::string supervisor_log_code(const std::filesystem::path &log) {
    std::string code;

    try {
        if (!std::filesystem::is_symlink(log) && std::filesystem::is_regular_file(log) &&
            std::filesystem::hard_link_count(log) == 1) {
            std::ifstream input(log, std::ios::binary);
            input.seekg(0, std::ios::end);
            const auto length = input.tellg();

            if (length >= 0) {
                const auto amount = std::min<std::streamoff>(length, 65536);
                input.seekg(length - amount);
                std::string tail(static_cast<std::size_t>(amount), '\0');
                input.read(tail.data(), amount);

                if (input) {
                    code = supervisor_failure_code(tail);
                }
            }
        }
    } catch (const std::exception
                     &) { /* Ignored: the log path is reported without a retained copy. */
    }

    return code;
}

// The failure of a start whose supervisor exited: the structured code of its log, or its exit
// status without one, and the path of the log.
inline cli::LocalizedError supervisor_exit_error(const std::filesystem::path &log, int status) {
    const auto code = supervisor_log_code(log);

    return {"SUPERVISOR_EXITED",
            "error.start_failed",
            {{"cause", code.empty() ? "exit " + std::to_string(status) : code},
             {"log", std::filesystem::absolute(log).string()}}};
}
} // namespace voyemu
