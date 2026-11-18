#include "graphics_ram.hpp"
#include "check.hpp"
#include "graphics.hpp"
#include "model.hpp"
#include "process.hpp"
#include "qnx_process_ram.hpp"
#include <algorithm>
#include <array>
#include <exception>
#include <fstream>
#include <iostream>
#include <limits>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

using namespace voyemu;
using namespace voyemu::graphics;

namespace {
constexpr Check need{"GRAPHICS_RAM_TEST"};

template <class F> void rejected(F action, const char *message) {
    bool failed = false;

    try {
        action();
    } catch (const VoyemuError &error) {
        need(std::string(error.what()) == std::string("GRAPHICS: ") + message, error.what());
        failed = true;
    }

    need(failed, "invalid RAM operation accepted");
}

bool symlink_fixture(const std::filesystem::path &target, const std::filesystem::path &link) {
    std::error_code error;

    std::filesystem::create_symlink(target, link, error);
#ifdef _WIN32
    if (error) {
        need(error.value() == ERROR_PRIVILEGE_NOT_HELD || error.value() == ERROR_NOT_SUPPORTED,
             error.message());
        std::cerr << "symlink fixture unavailable: " << error.message() << '\n';

        return false;
    }
#else
    need(!error, error.message());
#endif

    return true;
}

void mapped_backing(const std::filesystem::path &root) {
    const auto directory = root / "active", parked = root / "parked";

    ensure_private_directory(directory);
    const auto path = directory / "qnx-ram.bin";
    create_qnx_ram(path, ram_bytes);
    Options options;
    options.ram = path;
    const std::array written{std::byte{0x17}, std::byte{0xc4}, std::byte{0x80}, std::byte{0x2a}};

    {
        detail::Ram first(options);
        first.check_identity();
        const auto link = directory / "ram-link.bin";

        if (symlink_fixture(path, link)) {
            auto linked = options;
            linked.ram = link;
#ifdef _WIN32
            rejected(
                    [&] {
                        detail::Ram invalid(linked);
                    },
                    "RAM backing geometry");
#else
            rejected(
                    [&] {
                        detail::Ram invalid(linked);
                    },
                    "RAM backing open");
#endif
            std::filesystem::remove(link);
        }

        {
            detail::Ram second(options);
            first.write(ram_base + 32, written);
            const auto observed = second.extent(ram_base + 32, written.size());
            need(std::ranges::equal(observed, written), "write missing from another mapping");
            auto writer = second.extent(ram_base + 64, written.size());
            std::ranges::copy(written, writer.begin());
            need(std::ranges::equal(first.extent(ram_base + 64, written.size()), written),
                 "mapped writer missing from first mapping");
            first.write(ram_base + 64, first.extent(ram_base + 64, written.size()));
            need(std::ranges::equal(writer, written), "self write changed mapped bytes");
        }

        for (const auto address :
             {ram_base - 1, ram_base + ram_bytes, std::numeric_limits<std::uint64_t>::max()}) {
            rejected(
                    [&] {
                        first.extent(address, 1);
                    },
                    "RAM transfer geometry");
            rejected(
                    [&] {
                        first.write(address, written);
                    },
                    "RAM transfer geometry");
        }

        rejected(
                [&] {
                    first.extent(ram_base, 0);
                },
                "RAM transfer geometry");
        rejected(
                [&] {
                    first.extent(ram_base, (64ULL << 20) + 1);
                },
                "RAM transfer geometry");
        rejected(
                [&] {
                    first.extent(ram_base + ram_bytes - 1, 2);
                },
                "RAM transfer geometry");
        rejected(
                [&] {
                    first.write(ram_base + ram_bytes - 1, written);
                },
                "RAM transfer geometry");
        rejected(
                [&] {
                    first.write(ram_base, {});
                },
                "RAM transfer geometry");

        std::error_code rename_error;
        std::filesystem::rename(directory, parked, rename_error);

#ifdef _WIN32
        if (rename_error) {
            need(rename_error == std::errc::permission_denied ||
                         rename_error.value() == ERROR_SHARING_VIOLATION,
                 rename_error.message());
            need(std::filesystem::exists(path) && !std::filesystem::exists(parked),
                 "failed rename displaced the mapped backing");
        } else
#endif
        {
            need(!rename_error, rename_error.message());
            ensure_private_directory(directory);
            create_qnx_ram(path, ram_bytes);
            rejected(
                    [&] {
                        first.check_identity();
                    },
                    "RAM backing changed");
            std::filesystem::remove_all(directory);
            std::filesystem::rename(parked, directory);
        }

        first.check_identity();

#ifndef _WIN32
        const auto original = directory / "original.bin";
        std::filesystem::rename(path, original);
        std::filesystem::create_symlink(original, path);
        rejected(
                [&] {
                    first.check_identity();
                },
                "RAM backing changed");
        rejected(
                [&] {
                    detail::Ram linked(options);
                },
                "RAM backing open");
        std::filesystem::remove(path);
        std::filesystem::rename(original, path);
        first.check_identity();
#endif
    }

    std::ifstream file(path, std::ios::binary);
    std::array<std::byte, 4> actual{};
    file.seekg(32);
    file.read(reinterpret_cast<char *>(actual.data()), actual.size());
    need(bool(file) && actual == written, "write missing from backing file");
    file.seekg(64);
    file.read(reinterpret_cast<char *>(actual.data()), actual.size());
    need(bool(file) && actual == written, "mapped writer missing from backing file");
    file.close();
#ifdef _WIN32
    // The cross-build's filesystem resize can narrow the sparse file size to 32 bits.
    const HANDLE backing = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                                       OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    need(backing != INVALID_HANDLE_VALUE, "open fixture RAM for truncation");
    LARGE_INTEGER shorter{};
    shorter.QuadPart = ram_bytes - 4096;
    const bool truncated = SetFilePointerEx(backing, shorter, nullptr, FILE_BEGIN) &&
                           SetEndOfFile(backing);
    CloseHandle(backing);
    need(truncated && std::filesystem::file_size(path) == ram_bytes - 4096,
         "wrong-size RAM fixture extent");
#else
    std::filesystem::resize_file(path, ram_bytes - 4096);
#endif
    rejected(
            [&] {
                detail::Ram wrong_size(options);
            },
            "RAM backing geometry");
    // The failing constructor must close the backing handle, including on Windows.
    need(std::filesystem::remove(path), "failed constructor retained backing file");
    rejected(
            [&] {
                detail::Ram absent(options);
            },
            "RAM backing open");
    options.ram_extent = ram_bytes - 1;
    rejected(
            [&] {
                detail::Ram wrong_extent(options);
            },
            "RAM configured geometry");
}

void control_file(const std::filesystem::path &root) {
    const auto path = root / "control.json";
    const std::string value = "{\"schema\":1}\n";
    {
        std::ofstream file(path, std::ios::binary);
        file << value;
        need(bool(file), "control fixture write");
    }

    need(detail::read_control_file(path, value.size()) == value, "control file bytes changed");
#ifdef _WIN32
    rejected(
            [&] {
                detail::read_control_file(path, value.size() + 1);
            },
            "Control file read");
#else
    rejected(
            [&] {
                detail::read_control_file(path, value.size() + 1);
            },
            "Control file race");
#endif
    const auto link = root / "control-link.json";

    if (symlink_fixture(path, link)) {
#ifdef _WIN32
        rejected(
                [&] {
                    detail::read_control_file(link, value.size());
                },
                "Control file race");
#else
        rejected(
                [&] {
                    detail::read_control_file(link, value.size());
                },
                "Control file open");
#endif
    }

    std::filesystem::remove(path);
    rejected(
            [&] {
                detail::read_control_file(path, value.size());
            },
            "Control file open");
}
} // namespace

int main(int argc, char **argv) try {
    need(argc == 2, "usage: graphics_ram_test DIRECTORY");
    ensure_private_directory(argv[1]);
    const auto root = std::filesystem::canonical(argv[1]) / ("graphics-ram-test-" + random_token());
    ensure_private_directory(root);
    struct Cleanup {
        std::filesystem::path path;

        ~Cleanup() {
            std::error_code error;
            std::filesystem::remove_all(path, error);
        }
    } cleanup{root};
    mapped_backing(root);
    control_file(root);
} catch (const std::exception &error) {
    std::cerr << error.what() << '\n';

    return 1;
}
