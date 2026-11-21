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
    Json request_failure, response_failure, dispatched_response;

    try {
        static_cast<void>(Json::parse("{"));
    } catch (const Json::exception &json) {
        request_failure = error(json);
        response_failure = error(json, true);
        dispatched_response = error(json, true, true);
    }

    assert(request_failure.at("code") == "TEST_ARGUMENT" &&
           request_failure.at("message") == "Invalid JSON request shape" &&
           not_started(request_failure));
    assert(response_failure.at("code") == "IPC_PROTOCOL" &&
           response_failure.at("message") == "Invalid JSON response shape" &&
           not_started(response_failure));
    assert(dispatched_response.at("code") == "IPC_PROTOCOL_UNKNOWN" &&
           dispatched_response.at("outcome") == "unknown" &&
           dispatched_response.at("retryable") == false);

    const auto unknown = error(std::logic_error("vector index out of range"));
    assert(unknown.at("code") == "INTERNAL_ERROR" &&
           unknown.at("message") == "vector index out of range" && not_started(unknown));
    // After an action was dispatched, a failure without its own CODE leaves its effect unknown;
    // the failure keeps its kind and cause.
    const auto unknown_outcome = [](const Json &failure) {
        return failure.at("outcome") == "unknown" && failure.at("retryable") == false;
    };
    const auto dispatched_filesystem = error(denied, true, true);
    assert(dispatched_filesystem.at("code") == "FILESYSTEM_UNKNOWN" &&
           dispatched_filesystem.at("message") == denied.what() &&
           unknown_outcome(dispatched_filesystem));
    const auto dispatched_system = error(refused, true, true);
    assert(dispatched_system.at("code") == "SYSTEM_UNKNOWN" && unknown_outcome(dispatched_system));
    const auto dispatched_internal = error(std::logic_error("vector index out of range"), true,
                                           true);
    assert(dispatched_internal.at("code") == "INTERNAL_ERROR_UNKNOWN" &&
           dispatched_internal.at("message") == "vector index out of range" &&
           unknown_outcome(dispatched_internal));
    // A failure with its own CODE keeps its classification after dispatch.
    const auto busy = error(std::runtime_error("TEST_BUSY: writer active"), true, true);
    assert(busy.at("code") == "TEST_BUSY" && busy.at("outcome") == "not_started");
    // An own CODE wins over the kind of the exception that carries it.
    const auto coded = error(std::system_error(std::make_error_code(std::errc::timed_out),
                                               "CAN_TIMEOUT_UNKNOWN: read"));
    assert(coded.at("code") == "CAN_TIMEOUT_UNKNOWN" && coded.at("outcome") == "unknown" &&
           coded.at("retryable") == true);
} catch (const std::exception &failure) {
    std::cerr << failure.what() << '\n';

    return 1;
}
