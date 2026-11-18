#pragma once
#include "session_internal.hpp"
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <aclapi.h>

#include "wine_native_backend_windows.hpp"

// Named pipe, loopback QMP and file owner helpers of the Windows parts of Session.
namespace voyemu::session_detail {
std::wstring pipe_name(const std::filesystem::path &endpoint);
HANDLE pipe_connect(const std::filesystem::path &endpoint, int timeout);
void pipe_write(HANDLE pipe, const std::string &text);
void pipe_write_response(HANDLE pipe, const std::string &text,
                         ClusterDeadline deadline = std::chrono::steady_clock::now() +
                                                    std::chrono::seconds(2));
std::string pipe_read(HANDLE pipe, int timeout, std::size_t limit = maximum_message);
bool qmp_continue_windows(const Json &qmp, bool checkpoint,
                          const std::function<void()> &verify_owner = {},
                          std::optional<bool> persistence_paused = std::nullopt);
bool owned_windows_file(HANDLE file);
} // namespace voyemu::session_detail
