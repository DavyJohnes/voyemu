#include "library.hpp"
#include "check.hpp"
#include "core/process.hpp"
#include "model.hpp"
#include <exception>
#include <iostream>

using namespace voyemu;

namespace {
constexpr Check need{"LIBRARY_TEST"};
} // namespace

int main(int argc, char **argv) try {
    need(argc == 3, "usage: library_test FIXTURE DIRECTORY");
    const auto fixture = std::filesystem::canonical(argv[1]);
    ensure_private_directory(argv[2]);
    const auto root = std::filesystem::canonical(argv[2]) / ("library-test-" + random_token());
    ensure_private_directory(root);
    struct Cleanup {
        std::filesystem::path path;

        ~Cleanup() {
            std::error_code error;
            std::filesystem::remove_all(path, error);
        }
    } cleanup{root};
    auto filename = std::filesystem::path(u8"native-библиотека");
    filename += fixture.extension();
    const auto path = root / filename;
    std::filesystem::copy_file(fixture, path);

    {
        os::Library library(path);
        need(library.loaded(), library.failure());
        need(library.failure().empty(), "loaded library reported a failure");
        const auto function = reinterpret_cast<int (*)()>(library.symbol("voyemu_library_fixture"));
        need(function != nullptr && function() == 73, "explicit path loaded another library");
        need(library.symbol("voyemu_missing_symbol") == nullptr, "missing symbol resolved");
    }

    // Both literal and std::string callers keep the system-library overload.
    os::Library missing("voyemu-missing-library-6c9f9c10"),
            named(std::string("voyemu-missing-library-4bacbb73"));
    os::Library missing_path(root / "absent-library");

    for (const auto *library : {&missing, &named, &missing_path}) {
        need(!library->loaded() && !library->failure().empty(), "missing library loaded");
        need(library->symbol("voyemu_library_fixture") == nullptr,
             "unloaded library resolved a symbol");
    }

    need(std::filesystem::remove(path), "library handle retained after destruction");
} catch (const std::exception &error) {
    std::cerr << error.what() << '\n';

    return 1;
}
