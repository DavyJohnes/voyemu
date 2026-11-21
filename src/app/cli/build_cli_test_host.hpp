#pragma once
// The host part of build_cli.test.cpp: build_cli_posix.test.cpp and build_cli_windows.test.cpp.
#include <cstdint>

// Sends Ctrl+C to the child pid as SIGINT; false where the host has no such signal.
bool interrupt_child(std::int64_t pid);
