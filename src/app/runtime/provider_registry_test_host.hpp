#pragma once
// The host parts of provider_registry.test.cpp: provider_registry_posix.test.cpp and
// provider_registry_windows.test.cpp.
#include "provider_registry.hpp"
#include <filesystem>
#include <functional>

// Writes the provider reply to path readable by everyone: mode 0644 on POSIX, a read entry for
// Everyone on Windows.
void public_result(const std::filesystem::path &path, const voyemu::Json &reply);
// Windows pins the runtime executable against writes and replacement while the registry runs the
// real self-worker of manifest; POSIX has no such pin.
void pinned_self_worker_case(const voyemu::Json &manifest, const std::filesystem::path &self,
                             const std::filesystem::path &root, const std::filesystem::path &jobs,
                             const voyemu::runtime::ProviderContext &context,
                             const std::function<void()> &check_clean);
