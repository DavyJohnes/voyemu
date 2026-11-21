#pragma once
#include "model.hpp"
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>
namespace voyemu {
// Project files that deps, fetch, build and check read. A source checkout, a root with src/,
// reads its tree; an installation without src/ reads the copies of the few files listed in
// src/app/project_files.cmake that the build embedded in the launcher. Names are generic paths
// relative to the project root, such as "src/prepare/libraries.json".
struct ProjectFile {
    std::string name;
    std::uint64_t bytes = 0;
};
bool project_source_checkout(const std::filesystem::path &root);
// The SHA-256 of the regular file name, at most maximum bytes.
std::string project_file_sha256(const std::filesystem::path &root, std::string_view name,
                                std::uint64_t maximum);
std::string project_file_bytes(const std::filesystem::path &root, std::string_view name,
                               std::uint64_t maximum);
Json project_file_json(const std::filesystem::path &root, std::string_view name,
                       std::uint64_t maximum);
// The copy of name that the build embedded in the launcher, for a process without the project
// root.
Json embedded_project_file_json(std::string_view name, std::uint64_t maximum);
// The regular files directly in directory or, with recursive, anywhere below it, sorted by name.
std::vector<ProjectFile> project_directory_files(const std::filesystem::path &root,
                                                 std::string_view directory, bool recursive);
} // namespace voyemu
