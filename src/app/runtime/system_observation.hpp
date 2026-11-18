#pragma once
#include "model.hpp"
#include <functional>
#include <string>
#include <string_view>
#include <vector>
namespace voyemu {
std::string redact_diagnostic_text(std::string_view text,
                                   const std::vector<std::string> &secrets = {});
Json parse_system_components(std::string_view text, bool services);
Json parse_system_power(std::string_view text);
Json parse_system_processes(std::string_view text);
Json parse_android_logs(std::string_view text, bool details = false,
                        const std::vector<std::string> &secrets = {});
Json parse_system_anr(std::string_view text);
Json parse_anr_trace_inventory(std::string_view text);
std::string native_handler_diagnostics_command();
Json parse_native_handler_diagnostics(std::string_view text,
                                      const std::vector<std::string> &secrets = {});
Json parse_native_inventory(std::string_view text);
Json parse_native_prefix(std::string_view text, const std::vector<std::string> &secrets = {});
Json parse_anr_diagnostics(std::string_view text, const std::vector<std::string> &secrets = {});
std::string diagnostic_tombstone(const Json &operation, std::string_view source);
Json collect_retained_android_diagnostics(
        std::string_view source, const std::string &tombstone,
        const std::vector<std::string> &secrets,
        const std::function<std::string(std::vector<std::string>)> &read);
} // namespace voyemu
