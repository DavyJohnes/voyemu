#include "qnx_update_import.hpp"
#include "cli_messages.hpp"
#include "process.hpp"
#include "qnx_qinggan11.hpp"
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
         const std::vector<std::pair<std::string, std::string>> &entries) {
    std::string local, central;

    for (const auto &[name, bytes] : entries) {
        const auto at = local.size();
        word(local, 0x04034b50, 4);
        word(local, 20, 2);
        word(local, 0, 2);
        word(local, 0, 2);
        word(local, 0, 4);
        word(local, crc(bytes), 4);
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
        word(central, crc(bytes), 4);
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
        if (argc == 6 && std::string_view(argv[1]) == "--owner") {
            const auto root = std::filesystem::canonical(argv[2]);
            const auto config = read_private_json(root / "profiles" / argv[3] / "profile-qnx.json",
                                                  128 * 1024);
            const auto tools = read_private_json(root / "src/app/toolchain.json", 256 * 1024)
                                       .at("qnxUpdateCodec")
                                       .at(
#ifdef _WIN32
                                               "win32"
#else
                                               "darwin"
#endif
                                       );
            std::cout << import_qnx_update(std::filesystem::canonical(argv[4]),
                                           config.at("update").at("sha256").get<std::string>(),
                                           config.at("originals"),
                                           root / tools.at("file").get<std::string>(),
                                           tools.at("sha256").get<std::string>(),
                                           std::filesystem::absolute(argv[5]))
                                 .dump()
                      << '\n';

            return 0;
        }

        Json qinggan11_611 = Json::object();

        for (const auto &pin : qnx_qinggan11_611_originals) {
            qinggan11_611[pin.file] = {
                    {"file", pin.file}, {"bytes", pin.bytes}, {"sha256", pin.sha256}};
        }

        check(qnx_qinggan11_611_originals_match(qinggan11_611) &&
                      !qnx_qinggan11_originals_match(qinggan11_611),
              "exact pinned triple of the second qinggan11 adapter required");
        auto cross = qinggan11_611;
        cross["ifs2_la.img"]["sha256"] = qnx_qinggan11_originals[1].sha256;
        check(!qnx_qinggan11_611_originals_match(cross), "mixed 7.3 IFS rejected");
        Json qinggan11 = Json::object();

        for (const auto &pin : qnx_qinggan11_originals) {
            qinggan11[pin.file] = {
                    {"file", pin.file}, {"bytes", pin.bytes}, {"sha256", pin.sha256}};
        }

        check(qnx_qinggan11_originals_match(qinggan11), "exact three-role metadata selected");
        auto wrong_qinggan11 = qinggan11;
        wrong_qinggan11["hyp_la.img"]["bytes"] = std::uint64_t(7340031);
        check(!qnx_qinggan11_originals_match(wrong_qinggan11), "A11 size mutation selected");
        wrong_qinggan11 = qinggan11;
        wrong_qinggan11["system_la.img"]["sha256"] = std::string(64, '0');
        check(!qnx_qinggan11_originals_match(wrong_qinggan11), "A11 SHA mutation selected");
        qinggan11["ifs2_recovery_la.img"] = {{"file", "ifs2_recovery_la.img"}};
        check(!qnx_qinggan11_originals_match(qinggan11), "foreign recovery selected as A11");

        if (argc != 4) {
            rejects([] {
                import_qnx_update({}, "", Json::object(), {}, "", {}, 0);
            });
            std::cout << "QNX owner update import argument guards passed\n";

            return 0;
        }

        const auto library = std::filesystem::canonical(argv[1]),
                   root = std::filesystem::absolute(argv[3]);
        const std::string codec_pin = argv[2];
        check(!std::filesystem::exists(root), "fresh fixture root");
        std::filesystem::create_directory(root);
        struct Cleanup {
            std::filesystem::path path;

            ~Cleanup() {
                std::error_code error;
                std::filesystem::remove_all(path, error);
            }
        } cleanup{root};
        std::vector<std::pair<std::string, std::string>> members;
        Json definitions = Json::object();

        for (const auto *role :
             {"hyp_la.img", "ifs2_la.img", "system_la.img", "ifs2_recovery_la.img"}) {
            const std::string bytes = "synthetic-" + std::string(role),
                              name = "qnx/" + std::string(role);
            members.emplace_back(name, bytes);
            definitions[role] = {{"file", role},
                                 {"bytes", std::uint64_t(bytes.size())},
                                 {"sha256", sha256_bytes(bytes)},
                                 {"update", {{"kind", "raw"}, {"entries", Json::array({name})}}}};
        }

        const auto archive = root / "update.zip";
        zip(archive, members);
        const auto archive_pin = sha256_file(archive);
        auto run = [&](const Json &spec, const std::string &name,
                       std::string_view pin = std::string_view{}) {
            return import_qnx_update(archive, pin.empty() ? archive_pin : pin, spec, library,
                                     codec_pin, root / name, 0);
        };
        const auto result = run(definitions, "raw");
        check(result.at("native") == true && result.at("originals").size() == 4, "raw receipt");

        for (const auto &[name, bytes] : members) {
            check(sha256_file(root / "raw" / std::filesystem::path(name).filename()) ==
                          sha256_bytes(bytes),
                  "raw source bytes");
        }

        rejects([&] {
            run(definitions, "raw");
        });
        rejects([&] {
            run(definitions, "bad-pin", std::string(64, '0'));
        });
        check(!std::filesystem::exists(root / "bad-pin"), "archive SHA before writes");
        auto wrong = definitions;
        wrong["hyp_la.img"]["sha256"] = std::string(64, '0');
        rejects([&] {
            run(wrong, "bad-image");
        });
        check(!std::filesystem::exists(root / "bad-image"), "transaction rollback");
        wrong = definitions;
        wrong["hyp_la.img"]["update"]["entries"] = Json::array({"../hyp_la.img"});
        rejects([&] {
            run(wrong, "bad-path");
        });
        check(!std::filesystem::exists(root / "bad-path"), "undeclared path before writes");
        // Each imported image reaches the progress meter; a volume without the reserve fails with
        // the disk space error before any output.
        std::uint64_t imported = 0, declared = 0;

        for (const auto &definition : definitions) {
            declared += definition.at("bytes").get<std::uint64_t>();
        }

        import_qnx_update(archive, archive_pin, definitions, library, codec_pin, root / "progress",
                          0, [&imported](std::uint64_t bytes) {
                              imported += bytes;
                          });
        check(imported == declared, "QNX import reports its image bytes");
        bool short_space = false;
        try {
            import_qnx_update(archive, archive_pin, definitions, library, codec_pin, root / "space",
                              std::numeric_limits<std::uint64_t>::max() / 4);
        } catch (const cli::LocalizedError &error) {
            short_space =
                    std::string_view(error.what())
                            .starts_with("QNX_IMPORT_DISK_PREFLIGHT: not enough disk space: ");
        }

        check(short_space && !std::filesystem::exists(root / "space"),
              "a volume without the reserve names the disk space");
#ifdef _WIN32
        const auto handle =
                LoadLibraryExW(library.c_str(), nullptr,
                               LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
        check(handle != nullptr, "codec load");
        auto symbol = [&](const char *name) {
            return reinterpret_cast<void *>(GetProcAddress(handle, name));
        };
#else
        const auto handle = dlopen(library.c_str(), RTLD_NOW | RTLD_LOCAL);
        check(handle != nullptr, "codec load");
        auto symbol = [&](const char *name) {
            return dlsym(handle, name);
        };
#endif
        const auto compress = reinterpret_cast<std::size_t (*)(
                void *, std::size_t, const void *, std::size_t, int)>(symbol("ZSTD_compress"));
        const auto is_error = reinterpret_cast<unsigned (*)(std::size_t)>(symbol("ZSTD_isError"));
        check(compress && is_error, "codec symbols");
        std::string source(131072, 0);
        source.replace(65534, 5, "value");
        std::string packed(source.size() + 1024, 0);
        const auto size = compress(packed.data(), packed.size(), source.data(), source.size(), 3);
        check(!is_error(size) && size > 2, "compressed fixture");
        packed.resize(size);
        members.erase(members.begin());
        const auto half = packed.size() / 2;
        members.emplace_back("qnx/hyp_la/hyp_la.patch1", packed.substr(half));
        members.emplace_back("qnx/hyp_la/hyp_la.patch0", packed.substr(0, half));
        zip(archive, members);
        auto patched = definitions;
        patched["hyp_la.img"] = {{"file", "hyp_la.img"},
                                 {"bytes", std::uint64_t(source.size())},
                                 {"sha256", sha256_bytes(source)},
                                 {"update",
                                  {{"kind", "zero-base-zstd"},
                                   {"entries", Json::array({"qnx/hyp_la/hyp_la.patch0",
                                                            "qnx/hyp_la/hyp_la.patch1"})}}}};
        const auto current_pin = sha256_file(archive);
        const auto restored = run(patched, "patched", current_pin);
        check(restored.at("originals").size() == 4 &&
                      sha256_file(root / "patched/hyp_la.img") == sha256_bytes(source),
              "ordered patch restore independent of ZIP order");
        wrong = patched;
        wrong["hyp_la.img"]["update"]["entries"] =
                Json::array({"qnx/hyp_la/hyp_la.patch1", "qnx/hyp_la/hyp_la.patch0"});
        rejects([&] {
            run(wrong, "bad-order", current_pin);
        });
        check(!std::filesystem::exists(root / "bad-order"), "fragment order before writes");
        wrong = patched;
        wrong["hyp_la.img"]["update"]["entries"] = Json::array({"qnx/hyp_la/hyp_la.patch0"});
        rejects([&] {
            run(wrong, "missing-fragment", current_pin);
        });
        check(!std::filesystem::exists(root / "missing-fragment"),
              "unlisted fragment before writes");
        members.pop_back();
        members.pop_back();
        members.emplace_back("qnx/hyp_la/hyp_la.patch1", packed);
        members.emplace_back("qnx/hyp_la/hyp_la.patch0", packed);
        zip(archive, members);
        auto segmented = patched;
        segmented["hyp_la.img"]["bytes"] = std::uint64_t(source.size() * 2);
        segmented["hyp_la.img"]["sha256"] = sha256_bytes(source + source);
        segmented["hyp_la.img"]["update"]["kind"] = "zero-base-zstd-segments";
        segmented["hyp_la.img"]["update"]["segment_bytes"] =
                Json::array({std::uint64_t(source.size()), std::uint64_t(source.size())});
        const auto segmented_pin = sha256_file(archive);
        run(segmented, "independent-segments", segmented_pin);
        check(sha256_file(root / "independent-segments/hyp_la.img") ==
                      sha256_bytes(source + source),
              "declared independent frames joined after decode");
        wrong = segmented;
        wrong["hyp_la.img"]["update"]["segment_bytes"] =
                Json::array({std::uint64_t(source.size() * 2)});
        rejects([&] {
            run(wrong, "wrong-segment-count", segmented_pin);
        });
        check(!std::filesystem::exists(root / "wrong-segment-count"),
              "wrong segment count before write");
        members.back().second = packed + packed;
        zip(archive, members);
        const auto trailing_pin = sha256_file(archive);
        rejects([&] {
            run(segmented, "trailing-segment-frame", trailing_pin);
        });
        check(!std::filesystem::exists(root / "trailing-segment-frame"),
              "undeclared trailing frame not accepted as segment");
#ifdef _WIN32
        FreeLibrary(handle);
#else
        dlclose(handle);
#endif
        std::cout
                << "Native owner update raw/ordered-zstd import, exact pins and transaction guards passed\n";

        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';

        return 1;
    }
}
