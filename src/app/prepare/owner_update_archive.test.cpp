#include "owner_update_archive.hpp"
#include "archive_fixture.hpp"
#include "process.hpp"
#include <fstream>
#include <iostream>
using namespace voyemu;

static void check(bool yes, const char *why) {
    if (!yes) {
        throw std::runtime_error(why);
    }
}

using Payload = ArchiveFixtureEntry;

static void stored_zip(const std::filesystem::path &file, const std::vector<Payload> &entries) {
    write_archive_fixture(file, entries, true);
}

static std::string bytes(const std::filesystem::path &path) {
    std::ifstream input(path, std::ios::binary);

    return {(std::istreambuf_iterator<char>(input)), {}};
}

static std::string optional_zip64(std::string source) {
    check(source.size() >= 22 && source.substr(source.size() - 22, 4) == std::string("PK\5\6", 4),
          "fixture ordinary EOCD");
    const auto end = source.substr(source.size() - 22);
    auto read = [&](unsigned at, unsigned count) {
        std::uint64_t value = 0;

        for (unsigned i = 0; i < count; ++i) {
            value |= std::uint64_t(static_cast<unsigned char>(end[at + i])) << (8 * i);
        }

        return value;
    };
    std::string extension(76, '\0');
    auto put = [&](unsigned at, std::uint64_t value, unsigned count) {
        for (unsigned i = 0; i < count; ++i) {
            extension[at + i] = static_cast<char>(value >> (8 * i));
        }
    };
    put(0, 0x06064b50, 4);
    put(4, 44, 8);
    put(12, 45, 2);
    put(14, 45, 2);
    put(24, read(8, 2), 8);
    put(32, read(10, 2), 8);
    put(40, read(12, 4), 8);
    put(48, read(16, 4), 8);
    put(56, 0x07064b50, 4);
    put(64, source.size() - 22, 8);
    put(72, 1, 4);
    source.insert(source.size() - 22, extension);

    return source;
}

int main() {
    try {
        const auto root = std::filesystem::canonical(std::filesystem::temp_directory_path()) /
                          ("voyemu-owner-" + random_token());
        ensure_private_directory(root);
        struct Cleanup {
            std::filesystem::path root;

            ~Cleanup() {
                std::filesystem::remove_all(root);
            }
        } cleanup{root};
        {
            const auto source = root / "split.zip";
            stored_zip(source, {{"parts/b", "BB"}, {"parts/a", "AAA"}, {"ignored", "unrelated"}});
            const Json pin = {{"bytes", std::filesystem::file_size(source)},
                              {"sha256", sha256_file(source)}};
            const Json image = {
                    {"bytes", 5u},
                    {"sha256", sha256_bytes("AAABB")},
                    {"segments",
                     Json::array({{{"member", "parts/a"}, {"offset", 0u}, {"bytes", 3u}},
                                  {{"member", "parts/b"}, {"offset", 3u}, {"bytes", 2u}}})}};
            const auto output = root / "split-success";
            const auto receipt = import_pinned_zip_image(source, pin, image, output, 0);
            check(bytes(output / "system.img") == "AAABB" &&
                          receipt.at("parts_materialized") == false,
                  "split streaming uses declared order without part copies");
            check(std::distance(std::filesystem::directory_iterator(output),
                                std::filesystem::directory_iterator{}) == 2,
                  "split producer writes only image and receipt");
            std::uint64_t written = 0;
            import_pinned_zip_image(source, pin, image, root / "split-progress", 0, {},
                                    PinnedZipImageKind::SplitFragments,
                                    [&written](std::uint64_t block) {
                                        written += block;
                                    });
            check(written == 5, "split import reports its image bytes");
            {
                const auto zero_zip = root / "zero-split.zip";
                const std::string zero(100000, '\0'), expected_zero = zero + "TAIL";
                stored_zip(zero_zip, {{"parts/b", "TAIL"}, {"parts/a", zero}});
                const Json zero_pin = {{"bytes", std::filesystem::file_size(zero_zip)},
                                       {"sha256", sha256_file(zero_zip)}};
                auto zero_image = image;
                zero_image["bytes"] = expected_zero.size();
                zero_image["sha256"] = sha256_bytes(expected_zero);
                zero_image["segments"][0]["bytes"] = zero.size();
                zero_image["segments"][1]["offset"] = zero.size();
                zero_image["segments"][1]["bytes"] = 4u;
                const auto target = root / "zero-split-output";
                import_pinned_zip_image(zero_zip, zero_pin, zero_image, target, 0);
                check(bytes(target / "system.img") == expected_zero,
                      "zero fragments preserve exact image bytes when written sparsely");
            }
            {
                const Json single = {
                        {"bytes", 3u},
                        {"sha256", sha256_bytes("AAA")},
                        {"segments",
                         Json::array({{{"member", "parts/a"}, {"offset", 0u}, {"bytes", 3u}}})}};

                for (unsigned mode = 0; mode < 7; ++mode) {
                    auto descriptor = single;

                    if (mode == 1) {
                        descriptor["segments"][0]["member"] = "../outside";
                    }

                    if (mode == 2) {
                        descriptor["segments"][0]["offset"] = 1u;
                    }

                    if (mode == 3) {
                        descriptor["segments"][0]["bytes"] = 4u;
                    }

                    if (mode == 4) {
                        descriptor["sha256"] = std::string(64, 'f');
                    }

                    if (mode == 5) {
                        descriptor["segments"].push_back(descriptor["segments"][0]);
                    }

                    const auto target = root / ("single-image-" + std::to_string(mode));
                    bool rejected = false;
                    try {
                        import_pinned_zip_image(
                                source, pin, descriptor, target, 0,
                                [&] {
                                    return mode == 6;
                                },
                                PinnedZipImageKind::SingleImage);
                    } catch (...) {
                        rejected = true;
                    }

                    check(mode == 0 ? !rejected && bytes(target / "system.img") == "AAA"
                                    : rejected && !std::filesystem::exists(target),
                          "single-image success and traversal/bounds/pin/type/cancel rollback");
                }

                bool rejected = false;
                try {
                    import_pinned_zip_image(source, pin, single, root / "split-single-denied", 0);
                } catch (...) {
                    rejected = true;
                }

                check(rejected, "split route retains minimum two fragments");
            }

            for (unsigned mode = 0; mode < 7; ++mode) {
                auto bad = image, src = pin;
                const auto target = root / ("split-rejected-" + std::to_string(mode));

                if (mode == 0) {
                    bad["segments"][1]["offset"] = 2u;
                }

                if (mode == 1) {
                    bad["segments"][1]["member"] = "parts/a";
                }

                if (mode == 2) {
                    bad["segments"][0]["member"] = "../outside";
                }

                if (mode == 3) {
                    bad["sha256"] = std::string(64, 'f');
                }

                if (mode == 4) {
                    bad["segments"][1]["member"] = "parts/missing";
                }

                if (mode == 5) {
                    src["sha256"] = std::string(64, 'f');
                }

                unsigned ticks = 0;
                bool denied = false;
                try {
                    import_pinned_zip_image(source, src, bad, target, 0, [&] {
                        return mode == 6 && ++ticks > 1;
                    });
                } catch (...) {
                    denied = true;
                }

                check(denied && !std::filesystem::exists(target),
                      "split rejection/cancellation must remove only fresh output");
                check(sha256_file(source) == pin.at("sha256").get<std::string>(),
                      "split source remains unchanged");
            }

            for (const auto &payloads :
                 {std::vector<Payload>{{"parts/a", "AAA"}, {"parts/a", "AAA"}, {"parts/b", "BB"}},
                  std::vector<Payload>{{"parts/a", "AAA", true}, {"parts/b", "BB"}},
                  std::vector<Payload>{{"parts/a", "AAA"}, {"parts/b", "BBB"}},
                  std::vector<Payload>{{"parts/a", "AAA", false, true}, {"parts/b", "BB"}}}) {
                const auto archive = root / ("split-malformed-" + random_token() + ".zip");
                stored_zip(archive, payloads);
                const Json descriptor = {{"bytes", std::filesystem::file_size(archive)},
                                         {"sha256", sha256_file(archive)}};
                const auto target = root / ("split-invalid-" + random_token());
                bool denied = false;
                try {
                    import_pinned_zip_image(archive, descriptor, image, target, 0);
                } catch (...) {
                    denied = true;
                }

                check(denied && !std::filesystem::exists(target),
                      "split duplicate/link/size/CRC rejection must roll back");
            }
        }
        {
            // Python tarfile/gzip fixture, independent of any host archiver.
            const std::string gzip_hex =
                    "1f8b08000000000000ffedd54b0e40301485614bb1035255e3da492d807885e5bb8cc44424ea1afcdfa44d27e74c4eda4d210b495cb970d61ea7b89ea238ddf777e79c4dd23c72afc33c4e6190c82fb27ec87baf5d018a3ad97f1339e37eff86fd2ba96bed06d0b4efbf6fd7a819cfffffca9486fd7f61088b760500000000000000000000c00b36dc838e8100280000";
            std::string gzip;

            for (std::size_t i = 0; i < gzip_hex.size(); i += 2) {
                gzip += char(std::stoul(gzip_hex.substr(i, 2), nullptr, 16));
            }

            const std::string expected = std::string("AAA") + std::string(3, '\0') + "BB";
            const Json system = {{"bytes", 8u},
                                 {"sha256", sha256_bytes(expected)},
                                 {"segments", Json::array({{{"member", "ota/a"},
                                                            {"offset", 0u},
                                                            {"bytes", 3u},
                                                            {"sha256", sha256_bytes("AAA")}},
                                                           {{"member", "ota/b"},
                                                            {"offset", 6u},
                                                            {"bytes", 2u},
                                                            {"sha256", sha256_bytes("BB")}}})}};
            const Json originals = {
                    {"role.img",
                     {{"file", "role.img"},
                      {"bytes", 3u},
                      {"sha256", sha256_bytes("raw")},
                      {"acquisition", {{"kind", "raw-tar-member"}, {"member", "ota/qnx"}}}}}};

            for (unsigned mode = 0; mode < 3; ++mode) {
                auto inner = gzip;

                if (mode == 1) {
                    inner[inner.size() - 8] ^= 1;
                }

                if (mode == 2) {
                    inner[inner.size() - 4] ^= 1;
                }

                const auto archive = root / ("portable-owner-" + std::to_string(mode) + ".zip"),
                           target = root / ("portable-owner-" + std::to_string(mode));
                stored_zip(archive, {{"update.tgz", inner}});
                const Json owner = {{"bytes", std::filesystem::file_size(archive)},
                                    {"sha256", sha256_file(archive)},
                                    {"inner",
                                     {{"file", "update.tgz"},
                                      {"bytes", inner.size()},
                                      {"sha256", sha256_bytes(inner)}}}};
                bool failed = false;
                try {
                    import_owner_update_archive(archive, owner, system, originals, target, 0);
                } catch (...) {
                    failed = true;
                }

                if (mode == 0) {
                    check(!failed && bytes(target / "system.img") == expected &&
                                  bytes(target / "qnx/role.img") == "raw",
                          "portable gzip tar scatter succeeds");
                } else {
                    check(failed && !std::filesystem::exists(target),
                          "gzip CRC/ISIZE corruption rolls back");
                }
            }

            {
                const std::string hex =
                        "1f8b08000000000000ffedd3bf0ac23010c7f1cc3e459ea0b934219d04b339fa0a0716dc941aa18fef3f103a7492a682dfcf9223cbdd71fc1ad7b8dd41c77dafc77e308b90b7b9577c173ff5ebdf4beb3b63c765c699ba5d8b0e8ff6357afd20df467bd172da9e8b3aad6ab3f6ee30a6c6d99fa14e31cee65f244cf31f24a5606c954cfe79fe73ce6b8f000000000000000000000000802fdc01f3f8d34300280000";
                std::string inner;

                for (std::size_t i = 0; i < hex.size(); i += 2) {
                    inner += char(std::stoul(hex.substr(i, 2), nullptr, 16));
                }

                const auto archive = root / "portable-pax.zip", target = root / "portable-pax";
                stored_zip(archive, {{"update.tgz", inner}});
                const Json owner = {{"bytes", std::filesystem::file_size(archive)},
                                    {"sha256", sha256_file(archive)},
                                    {"inner",
                                     {{"file", "update.tgz"},
                                      {"bytes", inner.size()},
                                      {"sha256", sha256_bytes(inner)}}}};
                const auto member = std::string("ota/") + std::string(110, 'a');
                const Json recipe = {
                        {"bytes", 3u},
                        {"sha256", sha256_bytes("AAA")},
                        {"segments", Json::array({{{"member", member},
                                                   {"offset", 0u},
                                                   {"bytes", 3u},
                                                   {"sha256", sha256_bytes("AAA")}}})}};
                import_owner_update_system(archive, owner, recipe, target, 0);
                check(bytes(target / "system.img") == "AAA",
                      "pax long path resolves selected member");
            }

            {
                const std::string hex =
                        "1f8b08000000000000ffedceb90d80300003c08cc2060411a5cf28990089677f222a4489c453dc3576674f6bed6b78566c724a4736d76cc6538f618839e714baf8f0afc3b6ac756ef36f6cfd5029e5eb0b000000000000000000dcb0031826c17d00280000";
                std::string inner;

                for (std::size_t i = 0; i < hex.size(); i += 2) {
                    inner += char(std::stoul(hex.substr(i, 2), nullptr, 16));
                }

                const auto archive = root / "portable-bad-tar.zip",
                           target = root / "portable-bad-tar";
                stored_zip(archive, {{"update.tgz", inner}});
                const Json owner = {{"bytes", std::filesystem::file_size(archive)},
                                    {"sha256", sha256_file(archive)},
                                    {"inner",
                                     {{"file", "update.tgz"},
                                      {"bytes", inner.size()},
                                      {"sha256", sha256_bytes(inner)}}}};
                const Json recipe = {
                        {"bytes", 3u},
                        {"sha256", sha256_bytes("AAA")},
                        {"segments", Json::array({{{"member", "ota/a"},
                                                   {"offset", 0u},
                                                   {"bytes", 3u},
                                                   {"sha256", sha256_bytes("AAA")}}})}};
                bool failed = false;
                try {
                    import_owner_update_system(archive, owner, recipe, target, 0);
                } catch (...) {
                    failed = true;
                }

                check(failed && !std::filesystem::exists(target),
                      "bad tar header checksum rolls back");
            }
            {
                const std::string hex =
                        "1f8b08000000000000ffedd3b10ac2301485e1ce3e459ea0b9ad214e82d91c7d85a075ab9118a18f5f8b8374e8244905ff6f49b8cb39cba975ad0f273f1c3b7fe9629585bc2dbd226df3f94ff7465ab1951af2d4997b3e928faff812593f4876ea1cfabebba5fd3d8670ddac5d084585e4b5cf9c318dda1ab3bc7fd9cef72fd65a53a9229bfcf3fd3be7d6ae000000000000000000000000802f8c9f4576e500280000";
                std::string inner;

                for (std::size_t i = 0; i < hex.size(); i += 2) {
                    inner += char(std::stoul(hex.substr(i, 2), nullptr, 16));
                }

                const auto archive = root / "portable-bad-pax.zip",
                           target = root / "portable-bad-pax";
                stored_zip(archive, {{"update.tgz", inner}});
                const Json owner = {{"bytes", std::filesystem::file_size(archive)},
                                    {"sha256", sha256_file(archive)},
                                    {"inner",
                                     {{"file", "update.tgz"},
                                      {"bytes", inner.size()},
                                      {"sha256", sha256_bytes(inner)}}}};
                const Json recipe = {
                        {"bytes", 3u},
                        {"sha256", sha256_bytes("AAA")},
                        {"segments", Json::array({{{"member", "ota/a"},
                                                   {"offset", 0u},
                                                   {"bytes", 3u},
                                                   {"sha256", sha256_bytes("AAA")}}})}};
                bool failed = false;
                try {
                    import_owner_update_system(archive, owner, recipe, target, 0);
                } catch (...) {
                    failed = true;
                }

                check(failed && !std::filesystem::exists(target),
                      "bad pax record length rolls back");
            }
        }

        const auto tgz = root / "update.tgz", zip = root / "owner.zip";
        const std::vector<Payload> entries{{"ota/b", "BB"}, {"ota/a", "AAA"}, {"ota/qnx", "raw"}};
        write_archive_fixture(tgz, entries, false);
        write_archive_fixture(zip, {{"update.tgz", bytes(tgz)}}, true);
        const Json owner = {{"bytes", std::filesystem::file_size(zip)},
                            {"sha256", sha256_file(zip)},
                            {"inner",
                             {{"file", "update.tgz"},
                              {"bytes", std::filesystem::file_size(tgz)},
                              {"sha256", sha256_file(tgz)}}}};
        const std::string expected = std::string("AAA") + std::string(3, '\0') + "BB";
        const Json system = {{"bytes", 8u},
                             {"sha256", sha256_bytes(expected)},
                             {"segments", Json::array({{{"member", "ota/a"},
                                                        {"offset", 0u},
                                                        {"bytes", 3u},
                                                        {"sha256", sha256_bytes("AAA")}},
                                                       {{"member", "ota/b"},
                                                        {"offset", 6u},
                                                        {"bytes", 2u},
                                                        {"sha256", sha256_bytes("BB")}}})}};
        const Json originals = {
                {"role.img",
                 {{"file", "role.img"},
                  {"bytes", 3u},
                  {"sha256", sha256_bytes("raw")},
                  {"acquisition", {{"kind", "raw-tar-member"}, {"member", "ota/qnx"}}}}}};
        std::uint64_t advanced = 0;
        const auto receipt = import_owner_update_archive(
                zip, owner, system, originals, root / "good", 0, [&advanced](std::uint64_t block) {
                    advanced += block;
                });
        check(advanced == system.at("bytes").get<std::uint64_t>(),
              "owner archive import reports the extent of its system image");
        check(bytes(root / "good/system.img") == expected &&
                      bytes(root / "good/qnx/role.img") == "raw" &&
                      receipt.at("guest_started") == false &&
                      !std::filesystem::exists(root / "good/.stage"),
              "native two-layer scatter/raw exact");
        const auto only_system = import_owner_update_system(zip, owner, system,
                                                            root / "system-only", 0);
        std::uint64_t direct_advanced = 0;
        const auto direct = import_owner_update_archive(
                tgz, owner, system, originals, root / "inner-only", 0, [&](std::uint64_t block) {
                    direct_advanced += block;
                });
        check(direct_advanced == system.at("bytes").get<std::uint64_t>(),
              "direct archive reports the complete system extent");
        check(direct.at("archive_sha256") == owner.at("inner").at("sha256") &&
                      direct.at("kind") == "native-owner-tgz-raw-scatter" &&
                      bytes(root / "inner-only/system.img") == expected &&
                      bytes(root / "inner-only/qnx/role.img") == "raw",
              "direct pinned inner archive verifies scatter and records its actual origin");
        check(bytes(root / "system-only/system.img") == expected &&
                      std::filesystem::is_empty(root / "system-only/qnx") &&
                      only_system.at("qnx").empty(),
              "explicit internal system-only import skips raw QNX roles");
        const auto extended = root / "optional-zip64.zip";
        {
            std::ofstream output(extended, std::ios::binary);
            output << optional_zip64(bytes(zip));
        }
        auto extended_owner = owner;
        extended_owner["bytes"] = std::filesystem::file_size(extended);
        extended_owner["sha256"] = sha256_file(extended);
        import_owner_update_archive(extended, extended_owner, system, originals,
                                    root / "optional-zip64", 0);
        check(bytes(root / "optional-zip64/system.img") == expected,
              "native backend accepts optional ZIP64 end with unsaturated legacy fields");
        unsigned attempt = 0;
        auto reject = [&](const auto &archive, const Json &definition, const Json &recipe,
                          const Json &raw) {
            const auto output = root / ("bad-" + std::to_string(++attempt));
            bool denied = false;
            try {
                import_owner_update_archive(archive, definition, recipe, raw, output, 0);
            } catch (...) {
                denied = true;
            }

            check(denied && !std::filesystem::exists(output),
                  "invalid acquisition rejected/rolled back");
        };
        auto bad_owner = owner;
        bad_owner["sha256"] = std::string(64, 'f');
        reject(zip, bad_owner, system, originals);
        bad_owner = owner;
        bad_owner["inner"]["sha256"] = std::string(64, 'f');
        reject(zip, bad_owner, system, originals);
        reject(tgz, bad_owner, system, originals);
        auto renamed = root / "renamed.tgz";
        std::filesystem::copy_file(tgz, renamed);
        reject(renamed, owner, system, originals);
        bad_owner = owner;
        bad_owner["inner"]["bytes"] = std::filesystem::file_size(tgz) + 1;
        reject(tgz, bad_owner, system, originals);
        const auto changed_dir = root / "changed";
        std::filesystem::create_directory(changed_dir);
        auto changed = bytes(tgz);
        changed.front() ^= 1;
        std::ofstream(changed_dir / "update.tgz", std::ios::binary) << changed;
        reject(changed_dir / "update.tgz", owner, system, originals);
        reject(zip, owner, system, Json::object());
        auto bad = system;
        bad["segments"][1]["offset"] = 2u;
        reject(zip, owner, bad, originals);
        bad = system;
        bad["segments"][0]["member"] = "../outside";
        reject(zip, owner, bad, originals);
        bad = system;
        bad["segments"][1]["sha256"] = std::string(64, 'f');
        reject(zip, owner, bad, originals);
        bad = system;
        bad["sha256"] = std::string(64, 'f');
        reject(zip, owner, bad, originals);
        bad = system;
        bad["segments"][1]["member"] = "ota/missing";
        reject(zip, owner, bad, originals);
        auto alias = root / "alias.zip";
        std::filesystem::create_symlink(zip, alias);
        reject(alias, owner, system, originals);
        std::filesystem::remove(alias);
        std::filesystem::create_hard_link(zip, alias);
        reject(alias, owner, system, originals);
        std::filesystem::remove(alias);

        for (unsigned mode = 0; mode < 2; ++mode) {
            auto invalid = entries;

            if (mode == 0) {
                invalid[0].link = true;
            } else {
                invalid.push_back(entries[0]);
            }

            const auto inner = root / ("invalid-" + std::to_string(mode) + ".tgz"),
                       outer = root / ("invalid-" + std::to_string(mode) + ".zip");
            write_archive_fixture(inner, invalid, false);
            write_archive_fixture(outer, {{"update.tgz", bytes(inner)}}, true);
            auto pins = owner;
            pins["bytes"] = std::filesystem::file_size(outer);
            pins["sha256"] = sha256_file(outer);
            pins["inner"]["bytes"] = std::filesystem::file_size(inner);
            pins["inner"]["sha256"] = sha256_file(inner);
            reject(outer, pins, system, originals);
        }

        bool refused = false;
        try {
            import_owner_update_archive(zip, owner, system, originals, root / "good", 0);
        } catch (...) {
            refused = true;
        }

        check(refused && bytes(root / "good/system.img") == expected, "existing output preserved");
        std::cout << "portable two-layer/scatter/raw rollback and pin/type/alias guards passed\n";

        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';

        return 1;
    }
}
