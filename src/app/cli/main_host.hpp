#pragma once
#include <optional>
namespace voyemu::cli {
// The result of a host-only internal command of the launcher, none for every other command: the
// _wine-native proxy on Windows in main_windows.cpp, none in main_posix.cpp.
std::optional<int> host_command(int argc, char **argv);
// Makes a broken local channel an I/O error instead of a signal that would orphan the owned VMs;
// Windows has no such signal.
void ignore_broken_pipe();
} // namespace voyemu::cli
