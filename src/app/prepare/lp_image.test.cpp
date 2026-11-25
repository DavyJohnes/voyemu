#include "lp_image.hpp"
#include "lp_image_fixture.hpp"
#include "model.hpp"
#include "portable_runtime.hpp"
#include "process.hpp"
#include <algorithm>
#include <array>
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

std::uint64_t field_value(std::string_view data, std::size_t at, unsigned bytes) {
    std::uint64_t value = 0;

    for (unsigned i = 0; i < bytes; ++i) {
        value |= std::uint64_t(static_cast<unsigned char>(data.at(at + i))) << (i * 8);
    }

    return value;
}

std::string mirrored_gpt(std::string disk, bool trailing_partition = false) {
    const auto last = disk.size() / 512 - 1;
    auto primary = disk.substr(512, 512), table = disk.substr(1024, 256);

    if (trailing_partition) {
        constexpr auto end_lba = (65536 + 131072) / 512;
        table.resize(384, 0);
        table.at(256) = 3;
        field(table, 288, end_lba, 8);
        field(table, 296, end_lba + 7, 8);
        table.at(312) = 'p';
        field(primary, 80, 3, 4);
    }

    field(disk, 450, 0xee, 1);
    field(disk, 454, 1, 4);
    field(disk, 458, last, 4);
    field(disk, 510, 0xaa55, 2);
    field(primary, 48, last - 2, 8);
    field(primary, 88, crc(table), 4);
    auto backup = primary;
    field(backup, 24, last, 8);
    field(backup, 32, 1, 8);
    field(backup, 72, last - 1, 8);

    for (auto *header : {&primary, &backup}) {
        field(*header, 16, 0, 4);
        field(*header, 16, crc(std::string_view(*header).substr(0, 92)), 4);
    }

    disk.replace(512, 512, primary);
    disk.replace(1024, table.size(), table);
    disk.replace((last - 1) * 512, table.size(), table);
    disk.replace(last * 512, 512, backup);
    disk.at(40 * 512) = 'v';

    return disk;
}

void check_grown_gpt(const std::filesystem::path &file, const std::string &source,
                     std::uint64_t growth) {
    std::ifstream stream(file, std::ios::binary);
    const std::string data((std::istreambuf_iterator<char>(stream)), {});
    const auto old_last = source.size() / 512 - 1, last = data.size() / 512 - 1;
    const auto primary = data.substr(512, 512), backup = data.substr(last * 512, 512);
    const auto table = data.substr(1024, 256);

    check(data.size() == source.size() + growth && field_value(data, 458, 4) == last &&
          field_value(primary, 32, 8) == last && field_value(backup, 24, 8) == last &&
          field_value(backup, 72, 8) == last - 1 && field_value(primary, 48, 8) == last - 2 &&
          field_value(backup, 48, 8) == last - 2 &&
          field_value(table, 168, 8) == field_value(source, 1024 + 168, 8) + growth / 512 &&
          table.substr(0, 128) == source.substr(1024, 128) &&
          data.substr((last - 1) * 512, 256) == table &&
          data.substr((old_last - 1) * 512, 1024) == std::string(1024, 0) &&
          data.substr(40 * 512, 8 * 512) == source.substr(40 * 512, 8 * 512));

    for (auto header : {primary, backup}) {
        const auto expected = field_value(header, 16, 4);
        field(header, 16, 0, 4);
        check(crc(std::string_view(header).substr(0, 92)) == expected &&
              field_value(header, 88, 4) == crc(table));
    }
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

        if ((argc == 6 || argc == 7) && std::string(argv[1]) == "--copy-seed") {
            const auto placement = argc == 7 ? LpScratchPlacement::grow_terminal_super
                                             : LpScratchPlacement::within_super;
            check(argc == 6 || std::string_view(argv[6]) == "--grow-terminal-super");
            const auto target = std::filesystem::absolute(argv[4]);
            const std::uint64_t reserve = argc == 7 ? 8ULL << 30 : 20ULL << 30;
            copy_portable_image(std::filesystem::absolute(argv[2]), argv[3], target, reserve);
            std::cout << seed_lp_overlay_scratch(target, argv[3], std::stoull(argv[5]), placement)
                                 .dump(2)
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

        auto gpt = mirrored_gpt(scratch_source);
        gpt.at(98304) = 's';
        gpt.at(106496) = 'e';
        const auto growth_mode = LpScratchPlacement::grow_terminal_super;
        // Actual API30 x86_64 geometry: the two-GiB scratch lacks 418 MiB on the pinned disk.
        constexpr std::uint64_t mib = 1ULL << 20;
        check(lp_scratch_growth_bytes((2048ULL - 1630) * mib, mib, 3083 * mib,
                                      lp_scratch_max_growth) == 418 * mib);

        for (const auto [deficit, disk, limit] : std::vector<std::array<std::uint64_t, 3>>{
                     {418 * mib, 3083 * mib, lp_scratch_growth_limit},
                     {lp_scratch_max_growth + 1, 3083 * mib, lp_scratch_max_growth},
                     {418 * mib, 3800 * mib, lp_scratch_max_growth}}) {
            bool rejected = false;
            try {
                lp_scratch_growth_bytes(deficit, mib, disk, limit);
            } catch (const std::runtime_error &error) {
                rejected = std::string_view(error.what())
                                   .ends_with("bounded scratch super growth required");
            }

            check(rejected);
        }

        constexpr std::uint64_t wanted = 131072, expansion = 45056;
        save(file, gpt);
        const auto fits = seed_lp_overlay_scratch(file, sha256_bytes(gpt), 8192, growth_mode);
        check(!fits.at("image_grown").get<bool>() &&
              std::filesystem::file_size(file) == gpt.size());

        save(file, gpt);
        const auto grown = seed_lp_overlay_scratch(file, sha256_bytes(gpt), wanted, growth_mode);
        check(grown.at("image_grown") == true && grown.at("super_growth_bytes") == expansion &&
              grown.at("payloads_unchanged") == true && grown.at("metadata_copies") == 4);
        check_grown_gpt(file, gpt, expansion);
        const auto grown_scratch = read_lp_partition(file, sha256_file(file), "scratch");
        check(grown_scratch.offset == 110592 && grown_scratch.bytes == wanted &&
              grown_scratch.super_bytes == 131072 + expansion &&
              sha256_file_range(file, grown_scratch.offset, wanted) ==
                      sha256_bytes(std::string(wanted, 0)));

        for (const auto &part : {original_system, original_ext}) {
            const auto actual = read_lp_partition(file, sha256_file(file), part.name);
            check(actual.offset == part.offset && actual.bytes == part.bytes &&
                  sha256_file_range(file, actual.offset, actual.bytes) ==
                          sha256_bytes(std::string_view(gpt).substr(part.offset, part.bytes)));
        }

        constexpr std::string_view growth_errors[] = {"scratch growth requires terminal super",
                                                      "bounded scratch super growth required",
                                                      "GPT backup checksums or entries differ",
                                                      "GPT backup checksums or entries differ",
                                                      "protective MBR differs",
                                                      "fresh scratch tail must be zero",
                                                      "fresh scratch tail must be zero"};

        for (unsigned mode = 0; mode < std::size(growth_errors); ++mode) {
            auto invalid = gpt;
            auto capacity = wanted;

            if (mode == 0) {
                invalid = mirrored_gpt(scratch_source, true);
            } else if (mode == 1) {
                capacity += lp_scratch_growth_limit;
            } else if (mode == 2) {
                invalid.at(invalid.size() - 512 + 16) ^= 1;
            } else if (mode == 3) {
                invalid.at(invalid.size() - 1024) ^= 1;
            } else if (mode == 4) {
                invalid.at(458) ^= 1;
            } else if (mode == 5) {
                invalid.at(65536 + 131072) ^= 1;
            } else {
                invalid.at(110592) ^= 1;
            }

            save(file, invalid);
            const auto pin = sha256_bytes(invalid);
            bool denied = false;
            try {
                seed_lp_overlay_scratch(file, pin, capacity, growth_mode);
            } catch (const std::runtime_error &error) {
                denied = std::string_view(error.what()).starts_with("LP_IMAGE: ") &&
                         std::string_view(error.what()).ends_with(growth_errors[mode]);
            }

            check(denied && sha256_file(file) == pin);
        }

        std::cout << "LP10.0 GPT/checksum/copy/name/table/extent guards passed; no image copied\n";

        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';

        return 1;
    }
}
