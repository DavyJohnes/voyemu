#include "update_image.hpp"
#include "cli_messages.hpp"
#include "os/platform.hpp"
#include "process.hpp"
#include <fstream>
#include <iostream>
#include <limits>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif
using namespace voyemu;
namespace {
void check(bool value, const char *reason) {
    if (!value) {
        throw std::runtime_error(reason);
    }
}

void word(std::string &bytes, std::uint64_t value, unsigned width) {
    for (unsigned i = 0; i < width; ++i) {
        bytes += static_cast<char>(value >> (i * 8));
    }
}

std::uint32_t crc(std::string_view bytes) {
    std::uint32_t value = ~0U;

    for (unsigned char byte : bytes) {
        value ^= byte;

        for (unsigned i = 0; i < 8; ++i) {
            value = (value >> 1) ^ ((value & 1) ? 0xedb88320 : 0);
        }
    }

    return ~value;
}

void zip(const std::filesystem::path &file,
         const std::vector<std::pair<std::string, std::string>> &entries,
         unsigned corrupt_crc_entry = ~0U) {
    std::string local, central;
    unsigned entry_index = 0;

    for (const auto &[name, bytes] : entries) {
        const auto at = local.size();
        word(local, 0x04034b50, 4);
        word(local, 20, 2);
        word(local, 0, 2);
        word(local, 0, 2);
        word(local, 0, 4);
        word(local, crc(bytes) ^ (entry_index == corrupt_crc_entry ? 1U : 0U), 4);
        word(local, bytes.size(), 4);
        word(local, bytes.size(), 4);
        word(local, name.size(), 2);
        word(local, 0, 2);
        local += name;
        local += bytes;
        word(central, 0x02014b50, 4);
        word(central, 20, 2);
        word(central, 20, 2);
        word(central, 0, 2);
        word(central, 0, 2);
        word(central, 0, 4);
        word(central, crc(bytes) ^ (entry_index == corrupt_crc_entry ? 1U : 0U), 4);
        word(central, bytes.size(), 4);
        word(central, bytes.size(), 4);
        word(central, name.size(), 2);
        word(central, 0, 2);
        word(central, 0, 2);
        word(central, 0, 2);
        word(central, 0, 2);
        word(central, 0, 4);
        word(central, at, 4);
        central += name;
        ++entry_index;
    }

    const auto at = local.size();
    local += central;
    word(local, 0x06054b50, 4);
    word(local, 0, 2);
    word(local, 0, 2);
    word(local, entries.size(), 2);
    word(local, entries.size(), 2);
    word(local, central.size(), 4);
    word(local, at, 4);
    word(local, 0, 2);
    std::ofstream output(file, std::ios::binary);
    output.write(local.data(), local.size());
    check(bool(output), "fixture ZIP write");
}

template <class Function> void rejects(Function function) {
    bool failed = false;

    try {
        function();
    } catch (const std::exception &) {
        failed = true;
    }

    check(failed, "invalid import accepted");
}
} // namespace

int main(int argc, char **argv) {
    try {
        if (argc != 2) {
            rejects([] {
                import_android_update_image({}, "", Json::object(), {}, "", {}, 0);
            });
            std::cout << "Android update image argument guards passed\n";

            return 0;
        }

        const auto project = std::filesystem::canonical(argv[1]);
        const auto manifest = read_private_json(project / "src/app/toolchain.json", 256 * 1024)
                                      .at("qnxUpdateCodec")
                                      .at(std::string(os::dependency_system));
        const auto installed_library = project / manifest.at("file").get<std::string>();

        if (!std::filesystem::is_regular_file(installed_library)) {
            std::cout << "Pinned codec is not installed; run voyemu deps\n";

            return 77;
        }

        const auto library = std::filesystem::canonical(installed_library);
        const auto codec_pin = manifest.at("sha256").get<std::string>();
        check(sha256_file(library) == codec_pin, "fixture codec SHA before loading");
        ensure_private_directory(project / ".local/build/tmp");
        const auto root = project / ".local/build/tmp" / ("update-image-fixture-" + random_token());
        check(std::filesystem::create_directory(root), "fresh fixture root");
        struct Cleanup {
            std::filesystem::path path;

            ~Cleanup() {
                std::error_code error;
                std::filesystem::remove_all(path, error);
            }
        } cleanup{root};
#ifdef _WIN32
        const auto handle =
                LoadLibraryExW(library.c_str(), nullptr,
                               LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
        check(handle != nullptr, "fixture codec load");
        auto symbol = [&](const char *name) {
            return reinterpret_cast<void *>(GetProcAddress(handle, name));
        };
#else
        const auto handle = dlopen(library.c_str(), RTLD_NOW | RTLD_LOCAL);
        check(handle != nullptr, "fixture codec load");
        auto symbol = [&](const char *name) {
            return dlsym(handle, name);
        };
#endif
        const auto compress = reinterpret_cast<std::size_t (*)(
                void *, std::size_t, const void *, std::size_t, int)>(symbol("ZSTD_compress"));
        const auto is_error = reinterpret_cast<unsigned (*)(std::size_t)>(symbol("ZSTD_isError"));
        check(compress && is_error, "fixture codec symbols");
        std::string source(131072, 0);
        source.replace(65534, 5, "value");
        std::string packed(source.size() + 1024, 0);
        const auto size = compress(packed.data(), packed.size(), source.data(), source.size(), 3);
        check(!is_error(size) && size > 2, "compressed fixture");
        packed.resize(size);
        std::vector<std::pair<std::string, std::string>> members;
        const auto archive = root / "update.zip";

        const Json raw = {
                {"la_system.img",
                 {{"file", "la_system.img"},
                  {"bytes", std::uint64_t(source.size())},
                  {"sha256", sha256_bytes(source)},
                  {"update",
                   {{"kind", "raw"}, {"entries", Json::array({"android/la_system.img"})}}}}}};
        zip(archive, {{"android/la_system.img", source}});
        import_android_update_image(archive, sha256_file(archive), raw, library, codec_pin,
                                    root / "raw-positive", 0);
        check(sha256_file(root / "raw-positive/la_system.img") == sha256_bytes(source),
              "portable raw ZIP import");
        zip(archive, {{"android/la_system.img", source}}, 0U);
        const auto crc_target = root / "raw-corrupt-crc";
        rejects([&] {
            import_android_update_image(archive, sha256_file(archive), raw, library, codec_pin,
                                        crc_target, 0);
        });
        check(!std::filesystem::exists(crc_target), "repinned ZIP CRC failure rolls back");
        const auto split = packed.size() / 2;
        zip(archive, {{"android/la_system/la_system.patch0", packed.substr(0, split)},
                      {"android/la_system/la_system.patch1", packed.substr(split)}});
        auto fragmented = raw;
        fragmented["la_system.img"]["update"] = {
                {"kind", "zero-base-zstd"},
                {"entries", Json::array({"android/la_system/la_system.patch0",
                                         "android/la_system/la_system.patch1"})}};
        import_android_update_image(archive, sha256_file(archive), fragmented, library, codec_pin,
                                    root / "fragmented-positive", 0);
        check(sha256_file(root / "fragmented-positive/la_system.img") == sha256_bytes(source),
              "portable ZIP fragments concatenate in declared order");
        members = {{"android/la_system/la_system.patch0", packed},
                   {"android/la_system/la_system.patch1", packed}};
        zip(archive, members);
        const auto android_pin = sha256_file(archive);
        const Json android = {
                {"la_system.img",
                 {{"file", "la_system.img"},
                  {"bytes", std::uint64_t(source.size() * 2)},
                  {"sha256", sha256_bytes(source + source)},
                  {"update",
                   {{"kind", "zero-base-zstd-segments"},
                    {"entries", Json::array({"android/la_system/la_system.patch0",
                                             "android/la_system/la_system.patch1"})},
                    {"segment_bytes", Json::array({std::uint64_t(source.size()),
                                                   std::uint64_t(source.size())})}}}}}};
        import_android_update_image(archive, android_pin, android, library, codec_pin,
                                    root / "android-positive", 0);
        check(sha256_file(root / "android-positive/la_system.img") == sha256_bytes(source + source),
              "Android shares ordered segmented decoder");
        // The restored bytes reach the progress meter; a volume without the reserve fails with the
        // disk space error before any output.
        std::uint64_t restored = 0;
        import_android_update_image(archive, android_pin, android, library, codec_pin,
                                    root / "android-progress", 0, [&restored](std::uint64_t bytes) {
                                        restored += bytes;
                                    });
        check(restored == source.size() * 2, "segmented import reports its restored bytes");
        bool short_space = false;
        try {
            import_android_update_image(archive, android_pin, android, library, codec_pin,
                                        root / "android-space",
                                        std::numeric_limits<std::uint64_t>::max() / 4);
        } catch (const cli::LocalizedError &error) {
            short_space =
                    std::string_view(error.what())
                            .starts_with("UPDATE_IMAGE_DISK_PREFLIGHT: not enough disk space: ");
        }

        check(short_space && !std::filesystem::exists(root / "android-space"),
              "a volume without the reserve names the disk space");

        for (unsigned fault = 0; fault < 4; ++fault) {
            auto wrong = android;

            if (fault == 0) {
                wrong["la_system.img"]["update"]["segment_bytes"][0] = 1u;
            }

            if (fault == 1) {
                wrong["la_system.img"]["sha256"] = std::string(64, '0');
            }

            if (fault == 2) {
                std::swap(wrong["la_system.img"]["update"]["entries"][0],
                          wrong["la_system.img"]["update"]["entries"][1]);
            }

            if (fault == 3) {
                wrong["foreign.img"] = wrong["la_system.img"];
                wrong.erase("la_system.img");
            }

            const auto target = root / ("android-negative-" + std::to_string(fault));
            rejects([&] {
                import_android_update_image(archive, android_pin, wrong, library, codec_pin, target,
                                            0);
            });
            check(!std::filesystem::exists(target),
                  "Android failure rolls back fresh owned output");
        }

        zip(archive, members, 1U);
        const auto segment_crc_target = root / "segment-corrupt-crc";
        rejects([&] {
            import_android_update_image(archive, sha256_file(archive), android, library, codec_pin,
                                        segment_crc_target, 0);
        });
        check(!std::filesystem::exists(segment_crc_target),
              "second segment CRC failure removes earlier staged segment");

#ifdef _WIN32
        FreeLibrary(handle);
#else
        dlclose(handle);
#endif
        std::cout
                << "Native Android raw, fragmented and segmented ZIP import, CRC and four rollback negatives passed\n";

        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';

        return 1;
    }
}
