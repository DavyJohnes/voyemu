#pragma once
#include <filesystem>
#include <string>

namespace voyemu::os {
// A host library retained for the life of the object; loaded() is false when the host loader has
// none or rejects it, and failure() says why.
class Library {
public:
    // A bare name uses the existing host system-library search.
    explicit Library(const std::string &name);

    explicit Library(const char *name) : Library(std::string(name)) {}

    // An explicit path uses the library's directory when resolving pinned dependencies on Windows.
    explicit Library(const std::filesystem::path &path);
    ~Library();
    Library(const Library &) = delete;
    Library &operator=(const Library &) = delete;

    bool loaded() const {
        return handle_ != nullptr;
    }

    const std::string &failure() const {
        return failure_;
    }

    // The exported symbol, or null when the library lacks it.
    void *symbol(const char *name) const;

private:
    void *handle_{};
    std::string failure_;
};
} // namespace voyemu::os
