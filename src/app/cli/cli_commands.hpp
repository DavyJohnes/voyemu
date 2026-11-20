#pragma once
#include "check.hpp"
#include "cli_arguments.hpp"
#include "error.hpp"
#include "model.hpp"
#include "nxp_presentation.hpp"
#include "os/fs.hpp"
#include "os/locale.hpp"
#include "os/process.hpp"
#include "passion_presentation.hpp"
#include "process.hpp"
#include "program_modes.hpp"
#include "progress.hpp"
#include "qinggan11_startup.hpp"
#include "session_recovery.hpp"
#include <atomic>
#include <functional>
#include <optional>
#ifdef _WIN32
#include "wine_native_backend_windows.hpp"
#endif
#include "android_java.hpp"
#include "android_launch_adb.hpp"
#include "api.hpp"
#include "deps.hpp"
#include "fetch.hpp"
#include "help.hpp"
#include "host_payload.hpp"
#include "human_output.hpp"
#include "java_runtime.hpp"
#include "java_toolchain.hpp"
#include "launch_audit.hpp"
#include "logging.hpp"
#include "machine_path.hpp"
#include "portable_resources.hpp"
#include "portable_runtime.hpp"
#include "private_state_security.hpp"
#include "profile_build.hpp"
#include "profile_catalog.hpp"
#include "profile_runtime.hpp"
#include "qnx_host.hpp"
#include "qnx_interguest_contract.hpp"
#include "qnx_process_ram.hpp"
#include "session.hpp"
#include "start_host_pins.hpp"
#include "start_machine_identity.hpp"
#include "supervisor_failure.hpp"
#include "supervisor_stop.hpp"
#include <algorithm>
#include <charconv>
#include <csignal>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <thread>
#ifdef _WIN32
#include <windows.h>
#else
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/file.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace voyemu::cli {
// Set by SIGINT and SIGTERM; long commands and the supervisor loop stop at their next check.
extern std::atomic<bool> interrupted;
// Prints a command result: indented JSON for a private start, human output otherwise.
using Emit = std::function<void(const Json &)>;
int test_command(const Arguments &args);
int list_command(const Arguments &args, const Emit &emit);
int fetch_command(const Arguments &args, const Emit &emit);
int deps_command(const Arguments &args, const Emit &emit);
int build_command(const Arguments &args, const Emit &emit);
int check_command(const Arguments &args, const Emit &emit);
// Stops the running session of a restart: a failure ends the command with its exit code,
// otherwise the command continues as start.
std::optional<int> restart_command(Arguments &args, const Emit &emit);
// status, stop and patch of the running session.
int session_command(const Arguments &args, const Emit &emit);
int start_command(Arguments &args, const Emit &emit);
// The private supervisor of a started session.
int supervise(Arguments args, const std::filesystem::path &current_program);
} // namespace voyemu::cli
