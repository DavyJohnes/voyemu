#pragma once
// The host parts of session.test.cpp: session_posix.test.cpp and session_windows.test.cpp.
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <vector>

// Runs the synthetic QMP endpoint of the persistence fixture that argv selects: a Unix socket on
// POSIX, loopback TCP on Windows; none when argv selects no fixture of this host.
std::optional<int> persistence_qmp_fixture(int argc, char **argv);
// Runs action while the durable disk descriptor handle names /dev/null, so that its flushes fail,
// and restores it afterwards. Windows cannot replace a handle in place and throws.
void with_failing_disk_flushes(std::intptr_t handle, const std::function<void()> &action);
// Whether no live process has pid.
bool process_gone(std::int64_t pid);
// Makes Administrators the default owner of new files, as under an elevated Windows Server token;
// the previous default owner for restoring, empty where the token keeps its owner or on POSIX.
std::vector<std::byte> elevated_default_owner();
void restore_default_owner(std::vector<std::byte> &previous);
// The directory choice of the Unix control and QMP sockets; nothing on Windows, whose control
// endpoints are named pipes.
void project_socket_root_guard();
// Sixteen idle Unix socket clients of a control server at endpoint fill its worker slots and the
// next request gets the busy reply; nothing on Windows.
void control_server_idle_clients(const std::filesystem::path &endpoint);
// A peer that closes after its busy reply lets the request read that reply, and other early
// replies fail the write; nothing on Windows.
void control_server_early_busy_reply();
