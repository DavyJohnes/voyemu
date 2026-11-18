#pragma once
#include "guest_android.hpp"
#include "guest_qnx.hpp"
#include "media_wire.hpp"
#include "model.hpp"
#include "process.hpp"
#include <chrono>
#include <functional>
#include <memory>
namespace voyemu::api {
// Injectable owned transports keep host contract tests independent of ports, VM and OEM inputs.
struct Transports {
    std::function<Json(const std::filesystem::path &, const Json &, int)> control;
    std::function<Json(const Json &)> create;
    // Null guests dispatch to the owning supervisor through control; the supervisor Android
    // adapter reads SDK frames from the session media connection.
    std::shared_ptr<GuestAndroid> android;
    std::shared_ptr<GuestQnx> qnx;
};
using SourceStartCapture = std::function<ProcessResult(const ProcessSpec &, int)>;
Json capture_source_start(const ProcessSpec &process, bool windows,
                          const SourceStartCapture &capture);
std::string diagnostic_machine_name(const Json &status, const Json &selection);
using MediaInput = std::function<Json(const Json &)>;
void validate_media_input(const Json &input);
MediaInput media_input(const std::filesystem::path &directory, const std::string &session_id);
class Api {
public:
    Api(std::filesystem::path root, std::filesystem::path executable, Transports transports = {});
    Json call(const Json &request);

private:
    Json dispatch(const Json &request, std::chrono::steady_clock::time_point deadline = {});
    // Domain handlers of dispatch; each returns a discarded value for an operation of another
    // domain.
    Json dispatch_catalog(const std::string &op, const Json &request,
                          std::chrono::steady_clock::time_point deadline);
    Json dispatch_session(const std::string &op, const Json &request,
                          std::chrono::steady_clock::time_point deadline);
    Json dispatch_lifecycle(const std::string &op, const Json &request,
                            std::chrono::steady_clock::time_point deadline);
    Json dispatch_input(const std::string &op, const Json &request,
                        std::chrono::steady_clock::time_point deadline);
    Json dispatch_observe(const std::string &op, const Json &request,
                          std::chrono::steady_clock::time_point deadline);
    Json session_record(const Json &request, bool mutation);
    Json control(const Json &request, const Json &operation, bool mutation = false,
                 int timeout_ms = 2000);
    Json capture(const Json &request);
    Json anr(const Json &request);
    Json tree(const Json &request, std::chrono::steady_clock::time_point deadline = {},
              bool parallel = false);
    Json cluster_tree(const Json &request, std::chrono::steady_clock::time_point deadline = {});
    Json session_ui(const Json &request, std::chrono::steady_clock::time_point deadline = {});
    Json run(const Json &request);
    std::filesystem::path root_, executable_;
    Transports transports_;
};
int result_exit_code(const Json &response);
} // namespace voyemu::api
