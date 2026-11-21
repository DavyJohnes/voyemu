#pragma once
// The host parts of android_adb.test.cpp: android_adb_posix.test.cpp and
// android_adb_windows.test.cpp.
#include <cstdint>
#include <filesystem>
#include <string>

// The output of an identity probe child: Windows reads it while the child may still hold it open.
std::string read_probe_output(const std::filesystem::path &path);
// Waits up to 5 s for the child pid to exit on Windows, whose inherited output handle keeps its
// probe slot from being renamed; true at once on POSIX.
bool await_probe_child(std::int64_t pid);
// Keeps standard output from translating LF to CRLF on Windows.
void binary_standard_output();
