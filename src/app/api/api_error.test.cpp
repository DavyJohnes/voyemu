#include "api_internal.hpp"
#include <cassert>
#include <exception>
#include <iostream>
#include <system_error>
using namespace voyemu;
using voyemu::api::detail::error;

int main() try {
    const auto not_started = [](const Json &failure) {
        return failure.at("outcome") == "not_started" && failure.at("retryable") == false;
    };
    // An exception without a CODE keeps its kind and cause instead of INTERNAL_ERROR.
    const std::filesystem::filesystem_error denied(
            "resize_file", std::filesystem::path("/machines/car/qnx-ram.bin"),
            std::make_error_code(std::errc::permission_denied));
    const auto filesystem_failure = error(denied);

    assert(filesystem_failure.at("code") == "FILESYSTEM" &&
           filesystem_failure.at("message") == denied.what() &&
           filesystem_failure.at("message").get<std::string>().find("qnx-ram.bin") !=
                   std::string::npos &&
           not_started(filesystem_failure));
    const std::system_error refused(std::make_error_code(std::errc::connection_refused), "connect");
    const auto system_failure = error(refused);
    assert(system_failure.at("code") == "SYSTEM" &&
           system_failure.at("message") == refused.what() && not_started(system_failure));
    // Before any supervisor exchange a JSON exception reads the request shape, which the client
    // corrects; afterwards it reads a malformed response.
    Json request_failure, response_failure;

    try {
        static_cast<void>(Json::parse("{"));
    } catch (const Json::exception &json) {
        request_failure = error(json);
        response_failure = error(json, true);
    }

    assert(request_failure.at("code") == "TEST_ARGUMENT" &&
           request_failure.at("message") == "Invalid JSON request shape" &&
           not_started(request_failure));
    assert(response_failure.at("code") == "IPC_PROTOCOL" &&
           response_failure.at("message") == "Invalid JSON response shape" &&
           not_started(response_failure));

    const auto unknown = error(std::logic_error("vector index out of range"));
    assert(unknown.at("code") == "INTERNAL_ERROR" &&
           unknown.at("message") == "vector index out of range" && not_started(unknown));
    // An own CODE wins over the kind of the exception that carries it.
    const auto coded = error(std::system_error(std::make_error_code(std::errc::timed_out),
                                               "CAN_TIMEOUT_UNKNOWN: read"));
    assert(coded.at("code") == "CAN_TIMEOUT_UNKNOWN" && coded.at("outcome") == "unknown" &&
           coded.at("retryable") == true);
} catch (const std::exception &failure) {
    std::cerr << failure.what() << '\n';

    return 1;
}
