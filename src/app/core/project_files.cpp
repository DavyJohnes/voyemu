#include "project_files.hpp"
#include "error.hpp"
#include "process.hpp"
#include <algorithm>
#include <fstream>
#include <iterator>
namespace voyemu {
namespace {
struct EmbeddedProjectFile {
    std::string_view name;
    std::uint64_t bytes;
    const unsigned char *content;
};
#include "project_files.inc"

const EmbeddedProjectFile &embedded(std::string_view name) {
    const auto at = std::find_if(std::begin(embedded_project_files),
                                 std::end(embedded_project_files),
                                 [&](const EmbeddedProjectFile &file) {
                                     return file.name == name;
                                 });

    if (at == std::end(embedded_project_files)) {
        throw VoyemuError("PROJECT_FILE",
                          "project file is not embedded in the launcher: " + std::string(name));
    }

    return *at;
}

std::filesystem::path checked(const std::filesystem::path &root, std::string_view name,
                              std::uint64_t maximum) {
    const auto file = root / std::filesystem::path(std::string(name));

    if (std::filesystem::is_symlink(std::filesystem::symlink_status(file)) ||
        !std::filesystem::is_regular_file(file) || std::filesystem::file_size(file) > maximum) {
        throw VoyemuError("PROJECT_FILE",
                          "bounded ordinary project file required: " + file.string());
    }

    return file;
}
} // namespace

bool project_source_checkout(const std::filesystem::path &root) {
    return std::filesystem::is_directory(root / "src");
}

std::string project_file_sha256(const std::filesystem::path &root, std::string_view name,
                                std::uint64_t maximum) {
    if (project_source_checkout(root)) {
        return sha256_file(checked(root, name, maximum));
    }

    return sha256_bytes(project_file_bytes(root, name, maximum));
}

std::string project_file_bytes(const std::filesystem::path &root, std::string_view name,
                               std::uint64_t maximum) {
    if (project_source_checkout(root)) {
        std::ifstream input(checked(root, name, maximum), std::ios::binary);
        std::string bytes((std::istreambuf_iterator<char>(input)),
                          std::istreambuf_iterator<char>());

        if (!input.good() && !input.eof()) {
            throw VoyemuError("PROJECT_FILE", "project file read failed: " + std::string(name));
        }

        return bytes;
    }

    const auto &file = embedded(name);

    if (file.bytes > maximum) {
        throw VoyemuError("PROJECT_FILE",
                          "embedded project file exceeds its bound: " + std::string(name));
    }

    return {reinterpret_cast<const char *>(file.content), static_cast<std::size_t>(file.bytes)};
}

Json project_file_json(const std::filesystem::path &root, std::string_view name,
                       std::uint64_t maximum) {
    if (project_source_checkout(root)) {
        return read_private_json(checked(root, name, maximum), maximum);
    }

    return Json::parse(project_file_bytes(root, name, maximum));
}

std::vector<ProjectFile> project_directory_files(const std::filesystem::path &root,
                                                 std::string_view directory, bool recursive) {
    std::vector<ProjectFile> result;
    const std::string prefix = std::string(directory) + "/";

    if (project_source_checkout(root)) {
        const auto base = root / std::filesystem::path(std::string(directory));
        const auto add = [&](const std::filesystem::directory_entry &entry) {
            if (entry.is_regular_file() && !entry.is_symlink()) {
                result.push_back({entry.path().lexically_relative(root).generic_string(),
                                  entry.file_size()});
            }
        };

        if (recursive) {
            for (const auto &entry : std::filesystem::recursive_directory_iterator(base)) {
                add(entry);
            }
        } else {
            for (const auto &entry : std::filesystem::directory_iterator(base)) {
                add(entry);
            }
        }
    } else {
        for (const auto &file : embedded_project_files) {
            if (file.name.starts_with(prefix) &&
                (recursive || file.name.find('/', prefix.size()) == std::string_view::npos)) {
                result.push_back({std::string(file.name), file.bytes});
            }
        }
    }

    std::sort(result.begin(), result.end(), [](const ProjectFile &a, const ProjectFile &b) {
        return a.name < b.name;
    });

    return result;
}
} // namespace voyemu
