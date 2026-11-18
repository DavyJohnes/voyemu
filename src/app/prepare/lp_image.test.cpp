#include "lp_image.hpp"
#include "lp_image_fixture.hpp"
#include "model.hpp"
#include "portable_runtime.hpp"
#include "process.hpp"
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <vector>
using namespace voyemu;
using namespace voyemu::lp_fixture;
namespace {
void check(bool v) {
    if (!v) {
        throw std::runtime_error("LP fixture check failed");
    }
}

void save(const std::filesystem::path &p, std::string_view s) {
    std::ofstream out(p, std::ios::binary);

    out.write(s.data(), s.size());
    check(bool(out));
}
} // namespace

int main(int argc, char **argv) {
    try {
        if (argc == 5 && std::string(argv[1]) == "--image") {
            auto v = read_lp_partition(std::filesystem::absolute(argv[2]), argv[3], argv[4]);
            std::cout << Json{{"name", v.name},
                              {"offset", v.offset},
                              {"bytes", v.bytes},
                              {"super_offset", v.super_offset},
                              {"super_bytes", v.super_bytes},
                              {"metadata_sha256", v.metadata_sha256},
                              {"image_copied", false},
                              {"guest_started", false}}
                                 .dump()
                      << '\n';

            return 0;
        }

        if (argc == 6 && std::string(argv[1]) == "--copy-seed") {
            const auto target = std::filesystem::absolute(argv[4]);
            copy_portable_image(std::filesystem::absolute(argv[2]), argv[3], target, 20ULL << 30);
            std::cout << seed_lp_overlay_scratch(target, argv[3], std::stoull(argv[5])).dump(2)
                      << '\n';

            return 0;
        }

        if (argc == 5 && std::string(argv[1]) == "--seed-scratch") {
            std::cout << seed_lp_overlay_scratch(std::filesystem::absolute(argv[2]), argv[3],
                                                 std::stoull(argv[4]))
                                 .dump(2)
                      << '\n';

            return 0;
        }

        check(argc == 2);
        auto dir = std::filesystem::absolute(argv[1]) /
                   ("lp-image-" +
                    std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directories(dir);
        struct Cleanup {
            std::filesystem::path p;

            ~Cleanup() {
                std::error_code e;
                std::filesystem::remove_all(p, e);
            }
        } cleanup{dir};
        auto file = dir / "fixture.img";
        auto good = fixture();
        save(file, good);
        auto v = read_lp_partition(file, sha256_bytes(good));
        check(v.offset == 98304 && v.bytes == 8192 && v.super_offset == 65536 &&
              v.super_bytes == 131072);
        auto ext = read_lp_partition(file, sha256_bytes(good), "system_ext");
        check(ext.offset == 106496 && ext.bytes == 4096);
        auto reject = [&](std::string value) {
            save(file, value);
            bool bad = false;
            try {
                read_lp_partition(file, sha256_bytes(value));
            } catch (const std::runtime_error &e) {
                bad = std::string(e.what()).starts_with("LP_IMAGE:");
            }

            check(bad);
        };
        auto changed = good;
        changed[512 + 16] ^= 1;
        reject(changed);
        changed = good;
        changed[1024] ^= 1;
        reject(changed);
        changed = good;
        changed[65536 + 4096 + 8] ^= 1;
        reject(changed);
        changed = good;
        changed[65536 + 8192 + 40] ^= 1;
        reject(changed);
        changed = good;
        changed[65536 + 12288 + 12] ^= 1;
        reject(changed);
        changed = good;
        changed[65536 + 12288 + 128] ^= 1;
        reject(changed);

        for (auto edit : std::vector<std::function<void(std::string &)>>{[](auto &g) {
                                                                             field(g, 0, 1, 4);
                                                                         },
                                                                         [](auto &g) {
                                                                             field(g, 4, 48, 4);
                                                                         },
                                                                         [](auto &g) {
                                                                             field(g, 40, 513, 4);
                                                                         },
                                                                         [](auto &g) {
                                                                             field(g, 40, 2097152,
                                                                                   4);
                                                                         },
                                                                         [](auto &g) {
                                                                             field(g, 44, 0, 4);
                                                                         },
                                                                         [](auto &g) {
                                                                             field(g, 44, 5, 4);
                                                                         },
                                                                         [](auto &g) {
                                                                             field(g, 48, 512, 4);
                                                                         }}) {
            changed = good;
            geometry(changed, edit);
            reject(changed);
        }

        for (auto edit : std::vector<std::function<void(std::string &)>>{
                     [](auto &m) {
                         field(m, 6, 1, 2);
                     },
                     [](auto &m) {
                         field(m, 8, 256, 4);
                     },
                     [](auto &m) {
                         field(m, 80, 0xffffffff, 4);
                     },
                     [](auto &m) {
                         field(m, 84, 0xffffffff, 4);
                     },
                     [](auto &m) {
                         field(m, 88, 51, 4);
                     },
                     [](auto &m) {
                         m.replace(180, 10, std::string("system\0\0\0\0", 10));
                     },
                     [](auto &m) {
                         m.replace(128, 6, "absent");
                     },
                     [](auto &m) {
                         field(m, 128 + 36, 4, 4);
                     },
                     [](auto &m) {
                         field(m, 128 + 44, 2, 4);
                     },
                     [](auto &m) {
                         field(m, 128 + 48, 9, 4);
                     },
                     [](auto &m) {
                         field(m, 232 + 8, 1, 4);
                     },
                     [](auto &m) {
                         field(m, 232 + 20, 1, 4);
                     },
                     [](auto &m) {
                         field(m, 232, 0xffffffffffffffffULL, 8);
                     },
                     [](auto &m) {
                         field(m, 232 + 12, 1, 8);
                     },
                     [](auto &m) {
                         field(m, 268, 64, 8);
                     },
                     [](auto &m) {
                         field(m, 320, 8192, 8);
                     },
                     [](auto &m) {
                         field(m, 344, 1, 8);
                     },
                     [](auto &m) {
                         field(m, 328, 1, 8);
                     },
                     [](auto &m) {
                         field(m, 120, 2, 4);
                     }}) {
            changed = good;
            metadata(changed, edit);
            reject(changed);
        }

        changed = good;
        metadata(changed, [](auto &m) {
            m[128] = 'S';
        });
        changed.replace(65536 + 12288, 4096, good.substr(65536 + 12288, 4096));
        reject(changed);
        save(file, good);
        bool bad = false;
        try {
            read_lp_partition(file, std::string(64, '0'));
        } catch (...) {
            bad = true;
        }

        check(bad);
        bad = false;
        try {
            read_lp_partition(file, sha256_bytes(good), "missing");
        } catch (...) {
            bad = true;
        }

        check(bad);
        auto scratch_source = good;
        metadata(scratch_source, [](auto &m) {
            field(m, 320, 0, 8);
        });
        save(file, scratch_source);
        const auto original_system = read_lp_partition(file, sha256_bytes(scratch_source));
        const auto original_ext = read_lp_partition(file, sha256_bytes(scratch_source),
                                                    "system_ext");
        const auto seeded = seed_lp_overlay_scratch(file, sha256_bytes(scratch_source), 8192);
        check(seeded.at("payloads_unchanged") == true && seeded.at("scratch_formatted") == false &&
              seeded.at("metadata_copies") == 4 &&
              std::filesystem::file_size(file) == scratch_source.size());
        const auto scratch_view = read_lp_partition(file, sha256_file(file), "scratch");
        check(scratch_view.bytes == 8192 &&
              scratch_view.offset >= original_ext.offset + original_ext.bytes);
        check(sha256_file_range(file, original_system.offset, original_system.bytes) ==
              sha256_bytes(std::string_view(scratch_source)
                                   .substr(original_system.offset, original_system.bytes)));
        check(sha256_file_range(file, original_ext.offset, original_ext.bytes) ==
              sha256_bytes(std::string_view(scratch_source)
                                   .substr(original_ext.offset, original_ext.bytes)));

        for (unsigned mode = 0; mode < 10; ++mode) {
            auto requested = scratch_source;
            std::uint64_t bytes = 8192;

            if (mode == 0) {
                bytes = 0;
            }

            if (mode == 1) {
                bytes = 4097;
            }

            if (mode == 2) {
                bytes = (2ULL << 30) + 4096;
            }

            if (mode == 3) {
                bytes = 131072;
            }

            if (mode == 4) {
                requested = good;
            }

            if (mode == 5) {
                metadata(requested, [](auto &m) {
                    field(m, 336, 512, 4);
                });
            }

            if (mode == 6) {
                requested[65536 + 12288 + 12] ^= 1;
            }

            if (mode == 9) {
                requested[65536 + 45056] = 1;
            }

            if (mode == 7) {
                save(file, scratch_source);
                seed_lp_overlay_scratch(file, sha256_bytes(scratch_source), 8192);
                std::ifstream in(file, std::ios::binary);
                requested = std::string((std::istreambuf_iterator<char>(in)), {});
            }

            save(file, requested);
            const auto before = sha256_file(file);
            bool denied = false;
            try {
                seed_lp_overlay_scratch(file, mode == 8 ? std::string(64, '0') : before, bytes);
            } catch (...) {
                denied = true;
            }

            check(denied && sha256_file(file) == before);
        }

        std::cout << "LP10.0 GPT/checksum/copy/name/table/extent guards passed; no image copied\n";

        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';

        return 1;
    }
}
