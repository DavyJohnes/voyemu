#pragma once
// The host parts of provider_process.test.cpp: provider_process_posix.test.cpp and
// provider_process_windows.test.cpp.
#include <optional>

// The exit code of a provider child mode selected by argv; none for the test itself.
std::optional<int> provider_child_mode(int argc, char **argv);
// Runs owned providers that leave descendants, exit nonzero and outlive their deadline.
void provider_lifecycle_case(int argc, char **argv);
