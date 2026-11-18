#include "ordinary_path.hpp"
#include "check.hpp"
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
using namespace voyemu;

void check(bool value) {
    if (!value) {
        throw std::runtime_error("ordinary path contract differs");
    }
}

template <typename Work> void rejects(Work work, std::string_view code) {
    try {
        work();
    } catch (const VoyemuError &error) {
        check(error.code() == code);

        return;
    }

    throw std::runtime_error("invalid ordinary path accepted");
}
} // namespace

int main() {
    try {
        const auto jobs = std::filesystem::current_path() / ".local" / "build" / "jobs";
        std::filesystem::create_directories(jobs);
        const auto root =
                std::filesystem::canonical(jobs) /
                ("voyemu-ordinary-" +
                 std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directory(root);
        struct Cleanup {
            std::filesystem::path root;

            ~Cleanup() {
                std::error_code error;
                std::filesystem::remove_all(root, error);
            }
        } cleanup{root};
        const auto source = root / "source";
        std::filesystem::create_directory(source);
        std::ofstream(source / "file") << "source";
        const auto normalized = [](const std::filesystem::path &path) {
            ordinary_path(path, Check{"INPUT_PATH"}, PathForm::Normalized, "path", "link");
        };
        const auto canonical = [](const std::filesystem::path &path) {
            ordinary_path(path, Check{"INPUT_PATH"}, PathForm::Canonical, "path", "link");
        };
        const auto archive = [](const std::filesystem::path &path) {
            ordinary_path(path, Check{"OWNER_ARCHIVE"}, PathForm::CanonicalAncestors, "path",
                          "link");
        };
        const auto normalized_canonical = [](const std::filesystem::path &path) {
            ordinary_path(path, Check{"CANONICAL_INPUT"}, PathForm::NormalizedCanonical,
                          "canonical input", "link");
        };
        normalized(source / "file");
        canonical(source / "file");
        normalized(source);
        canonical(source);
        normalized_canonical(source / "file");
        // A destination's missing suffix is valid; an existing input's is not.
        normalized(root / "missing" / "output");
        archive(root / "missing" / "output");
        bool missing = false;
        try {
            canonical(root / "missing" / "output");
        } catch (const std::filesystem::filesystem_error &) {
            missing = true;
        }

        check(missing);
        rejects(
                [&] {
                    normalized("relative/output");
                },
                "INPUT_PATH");
        rejects(
                [&] {
                    normalized(root / "source" / ".." / "file");
                },
                "INPUT_PATH");
        rejects(
                [&] {
                    canonical(root / "source" / "." / "file");
                },
                "INPUT_PATH");
        // The normalized-input check must run before canonical() can throw for a missing path.
        rejects(
                [&] {
                    normalized_canonical(root / "absent" / ".." / "file");
                },
                "CANONICAL_INPUT");
        std::filesystem::create_directory_symlink(source, root / "alias");
        rejects(
                [&] {
                    normalized(root / "alias" / "file");
                },
                "INPUT_PATH");
        rejects(
                [&] {
                    archive(root / "alias" / "new");
                },
                "OWNER_ARCHIVE");
        // CodeCheck callers report their boundary-specific code, rather than a generic one.
        rejects(
                [&] {
                    ordinary_path(root / "alias" / "file", CodeCheck{"update input"},
                                  PathForm::Normalized, "UPDATE_PATH", "UPDATE_LINK");
                },
                "UPDATE_LINK");
        rejects(
                [&] {
                    ordinary_path("relative", CodeCheck{"update input"}, PathForm::Normalized,
                                  "UPDATE_PATH", "UPDATE_LINK");
                },
                "UPDATE_PATH");
        std::filesystem::create_hard_link(source / "file", root / "hard-link");
        canonical(root / "hard-link");
        check(std::filesystem::hard_link_count(root / "hard-link") == 2);
        std::cout << "ordinary path input, destination, ancestor and error contracts passed\n";

        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';

        return 1;
    }
}
