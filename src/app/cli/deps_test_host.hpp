#pragma once
// The host part of deps.test.cpp: deps_posix.test.cpp and deps_windows.test.cpp.
#include <filesystem>
#include <functional>

// Runs granted while users other than the owner may write file: group write on POSIX, a write
// entry for Everyone in the DACL on Windows; then restores the file.
void with_foreign_write(const std::filesystem::path &file, const std::function<void()> &granted);
