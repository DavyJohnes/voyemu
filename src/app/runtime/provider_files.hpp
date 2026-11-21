#pragma once
// The provider files of the host: POSIX descriptor walks in provider_files_posix.cpp, Windows
// handles that hold each ancestor in provider_files_windows.cpp. Every refusal throws
// PROVIDER_INVALID.
#include <filesystem>
#include <memory>
#include <string>

namespace voyemu::runtime::detail {
// The bytes of an ordinary single-link provider file of the current user, reached without links,
// at most limit bytes, private to the user when private_access is set.
std::string provider_read(const std::filesystem::path &path, std::size_t limit,
                          bool private_access);

// A pinned provider file; Windows holds it and its ancestors open until the object ends.
class ProviderPinnedFile {
public:
    ProviderPinnedFile(const std::filesystem::path &path, std::size_t limit, bool private_access);
    ~ProviderPinnedFile();
    ProviderPinnedFile(const ProviderPinnedFile &) = delete;
    ProviderPinnedFile &operator=(const ProviderPinnedFile &) = delete;
    std::string text();

private:
    struct State;
    std::unique_ptr<State> state_;
};

// A directory reached without links, private to the user when private_leaf is set, created when
// create_leaf is set and required to be new when fresh is set; Windows holds its ancestors open.
class ProviderDirectory {
public:
    ProviderDirectory(const std::filesystem::path &path, bool private_leaf = false,
                      bool create_leaf = false, bool fresh = false);
    ~ProviderDirectory();
    ProviderDirectory(const ProviderDirectory &) = delete;
    ProviderDirectory &operator=(const ProviderDirectory &) = delete;

private:
    struct State;
    std::unique_ptr<State> state_;
};

// Writes the request durably beside its final name and moves it there.
void provider_request(const std::filesystem::path &pending, const std::filesystem::path &request,
                      const std::string &bytes);
} // namespace voyemu::runtime::detail
