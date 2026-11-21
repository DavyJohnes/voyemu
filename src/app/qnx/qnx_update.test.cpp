#include "qnx_update.hpp"
#include "model.hpp"
#include "os/library.hpp"
#include "process.hpp"
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
using namespace voyemu;
namespace {
void check(bool value, const char *reason) {
    if (!value) {
        throw std::runtime_error(reason);
    }
}

void write(const std::filesystem::path &path, std::string_view value) {
    std::ofstream out(path, std::ios::binary);

    out.write(value.data(), value.size());
    check(bool(out), "fixture write");
}

template <class Function> void rejects(Function function) {
    bool rejected = false;

    try {
        function();
    } catch (const std::exception &) {
        rejected = true;
    }

    check(rejected, "invalid patch accepted");
}
} // namespace

int main(int argc, char **argv) {
    try {
        if (argc == 4 && std::string_view(argv[1]) == "--acquire-segments") {
            const auto request = read_private_json(std::filesystem::absolute(argv[2]), 128 * 1024);
            std::vector<QnxUpdateSegment> segments;

            for (const auto &item : request.at("segments")) {
                segments.push_back({std::filesystem::canonical(item.at("file").get<std::string>()),
                                    item.at("sha256").get<std::string>(),
                                    item.at("bytes").get<std::uint64_t>(),
                                    item.at("index").get<unsigned>()});
            }

            std::cout << acquire_qnx_zero_base_segments(
                                 segments, request.at("bytes").get<std::uint64_t>(),
                                 request.at("md5").get<std::string>(),
                                 std::filesystem::canonical(request.at("codec").get<std::string>()),
                                 request.at("codec_sha256").get<std::string>(),
                                 std::filesystem::absolute(argv[3]))
                      << '\n';

            return 0;
        }

        if (argc == 9 && std::string_view(argv[1]) == "--acquire") {
            std::cout << acquire_qnx_zero_base_patch(std::filesystem::canonical(argv[2]), argv[3],
                                                     std::stoull(argv[4]), argv[5],
                                                     std::filesystem::canonical(argv[6]), argv[7],
                                                     std::filesystem::absolute(argv[8]))
                      << '\n';

            return 0;
        }

        if (argc != 4) {
            rejects([] {
                restore_qnx_zero_base_patch({}, "", 0, "", {}, "", {});
            });
            std::cout << "Factory patch argument guards passed\n";

            return 0;
        }

        const auto library = std::filesystem::canonical(argv[1]),
                   directory = std::filesystem::absolute(argv[3]);
        const std::string library_pin = argv[2];
        check(sha256_file(library) == library_pin, "fixture codec pin");
        check(!std::filesystem::exists(directory), "fresh fixture directory required");
        std::filesystem::create_directory(directory);
        auto handle = std::make_unique<voyemu::os::Library>(std::filesystem::path(library));
        check(handle->loaded(), "fixture codec load");
        auto symbol = [&](const char *name) {
            return handle->symbol(name);
        };
        const auto compress = reinterpret_cast<std::size_t (*)(
                void *, std::size_t, const void *, std::size_t, int)>(symbol("ZSTD_compress"));
        const auto error = reinterpret_cast<unsigned (*)(std::size_t)>(symbol("ZSTD_isError"));
        check(compress && error, "fixture codec symbols");
        std::string original(196608, 0);
        original.replace(65535, 5, "value");
        original.replace(131071, 4, "test");
        std::string packed(original.size() + 1024, 0);
        const auto size = compress(packed.data(), packed.size(), original.data(), original.size(),
                                   3);
        check(!error(size) && size < packed.size(), "fixture compression");
        packed.resize(size);
        const auto patch = directory / "input.zstd", output = directory / "restored.img";
        write(patch, packed);
        const auto patch_pin = sha256_bytes(packed), image_pin = sha256_bytes(original);
        auto run = [&](std::uint64_t bytes, std::string_view image,
                       std::string_view input = std::string_view{}) {
            restore_qnx_zero_base_patch(patch, input.empty() ? patch_pin : input, bytes, image,
                                        library, library_pin, output);
        };
        run(original.size(), image_pin);
        check(std::filesystem::file_size(output) == original.size() &&
                      sha256_file(output) == image_pin,
              "roundtrip sparse extent/hash");
        rejects([&] {
            run(original.size(), image_pin);
        });
        std::filesystem::remove(output);
        rejects([&] {
            run(original.size(), image_pin, std::string(64, '0'));
        });
        check(!std::filesystem::exists(output), "bad pin created output");
        rejects([&] {
            run(original.size() - 1, image_pin);
        });
        check(!std::filesystem::exists(output), "oversize output retained");
        rejects([&] {
            run(original.size(), std::string(64, '0'));
        });
        check(!std::filesystem::exists(output), "wrong output hash retained");
        check(acquire_qnx_zero_base_patch(patch, patch_pin, original.size(),
                                          "af4b0606d5733cdc1218b96c29f3bc6d", library, library_pin,
                                          output) == image_pin,
              "OEM MD5 acquisition derives SHA");
        std::filesystem::remove(output);
        rejects([&] {
            acquire_qnx_zero_base_patch(patch, patch_pin, original.size(), std::string(32, '0'),
                                        library, library_pin, output);
        });
        check(!std::filesystem::exists(output), "wrong MD5 acquisition retained");
        std::vector<QnxUpdateSegment> segments{{patch, patch_pin, original.size(), 0},
                                               {patch, patch_pin, original.size(), 1}};
        restore_qnx_zero_base_segments(segments, original.size() * 2,
                                       sha256_bytes(original + original), library, library_pin,
                                       output);
        check(sha256_file(output) == sha256_bytes(original + original),
              "two declared independent frames restored");
        std::filesystem::remove(output);
        check(acquire_qnx_zero_base_segments(
                      segments, original.size() * 2, "29d1180dc766a6d3cf93cbed996f10d4", library,
                      library_pin, output) == sha256_bytes(original + original),
              "two-frame OEM MD5 acquisition");
        std::filesystem::remove(output);
        segments[0].index = 1;
        rejects([&] {
            restore_qnx_zero_base_segments(segments, original.size() * 2,
                                           sha256_bytes(original + original), library, library_pin,
                                           output);
        });
        check(!std::filesystem::exists(output), "wrong segment order before write");
        segments[0].index = 0;
        rejects([&] {
            restore_qnx_zero_base_segments(segments, original.size(), image_pin, library,
                                           library_pin, output);
        });
        check(!std::filesystem::exists(output), "wrong segment count/extent before write");
        auto broken = packed;
        broken.pop_back();
        write(patch, broken);
        rejects([&] {
            restore_qnx_zero_base_patch(patch, sha256_bytes(broken), original.size(), image_pin,
                                        library, library_pin, output);
        });
        check(!std::filesystem::exists(output), "truncated output retained");
        broken = packed + packed;
        write(patch, broken);
        rejects([&] {
            restore_qnx_zero_base_patch(patch, sha256_bytes(broken), original.size(), image_pin,
                                        library, library_pin, output);
        });
        check(!std::filesystem::exists(output), "extra frame retained");
        std::string dense(196608, 0);
        std::uint32_t random = 0x12345678;

        for (auto &byte : dense) {
            random ^= random << 13;
            random ^= random >> 17;
            random ^= random << 5;
            byte = static_cast<char>(random);
        }

        std::string dense_packed(dense.size() + 1024, 0);
        const auto dense_size = compress(dense_packed.data(), dense_packed.size(), dense.data(),
                                         dense.size(), 3);
        check(!error(dense_size) && dense_size > 65536, "multi-chunk fixture compression");
        dense_packed.resize(dense_size);
        write(patch, dense_packed);
        restore_qnx_zero_base_patch(patch, sha256_bytes(dense_packed), dense.size(),
                                    sha256_bytes(dense), library, library_pin, output);
        check(sha256_file(output) == sha256_bytes(dense), "multi-chunk decode");
        std::filesystem::remove(output);
        const auto create_context = reinterpret_cast<void *(*)()>(symbol("ZSTD_createCCtx"));
        const auto free_context =
                reinterpret_cast<std::size_t (*)(void *)>(symbol("ZSTD_freeCCtx"));
        const auto using_dictionary =
                reinterpret_cast<std::size_t (*)(void *, void *, std::size_t, const void *,
                                                 std::size_t, const void *, std::size_t, int)>(
                        symbol("ZSTD_compress_usingDict"));
        check(create_context && free_context && using_dictionary, "fixture dictionary symbols");
        void *context = create_context();
        check(context != nullptr, "fixture compression context");
        const auto foreign_source = dense.substr(dense.size() - 32768);
        std::string foreign_packed(foreign_source.size() + 1024, 0);
        const auto foreign_size = using_dictionary(
                context, foreign_packed.data(), foreign_packed.size(), foreign_source.data(),
                foreign_source.size(), dense.data(), dense.size(), 5);
        free_context(context);
        check(!error(foreign_size) && foreign_size < foreign_source.size() / 2,
              "fixture references nonzero dictionary");
        foreign_packed.resize(foreign_size);
        write(patch, foreign_packed);
        rejects([&] {
            restore_qnx_zero_base_patch(patch, sha256_bytes(foreign_packed), foreign_source.size(),
                                        sha256_bytes(foreign_source), library, library_pin, output);
        });
        check(!std::filesystem::exists(output), "nonzero-base patch retained");
        handle.reset();
        std::filesystem::remove(patch);
        std::filesystem::remove(directory);
        std::cout
                << "Upstream zstd factory patch sparse roundtrip and corruption/bounds guards passed\n";

        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';

        return 1;
    }
}
