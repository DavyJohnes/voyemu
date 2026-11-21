#pragma once
// The host parts of cli_flow.test.cpp: cli_flow_posix.test.cpp and cli_flow_windows.test.cpp.
#include "process.hpp"
#include <filesystem>
#include <string>
#include <vector>

// Runs the public CLI with its standard output and error in separate private files, so structured
// stderr diagnostics are never response authority; Windows only.
voyemu::ProcessResult fixture_public_cli(const std::filesystem::path &executable,
                                         const std::vector<std::string> &arguments,
                                         const std::filesystem::path &root);
