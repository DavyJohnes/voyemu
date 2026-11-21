#include "passion_generated_helper.hpp"
#include <cassert>
#include <fstream>
#include <iostream>
#include <vector>
using namespace voyemu;

template <class F> void rejects(F f) {
    bool bad = false;

    try {
        f();
    } catch (...) {
        bad = true;
    }

    assert(bad);
}

int main(int argc, char **argv) try {
    passion_generated_need(argc == 2, "project root argument required");
    const auto root = std::filesystem::canonical(argv[1]);
    const auto work = root / ".local/build/jobs" /
                      ("passion-generated-helper-test-" + random_token());
    ensure_private_directory(work);
    struct Cleanup {
        std::filesystem::path path;

        ~Cleanup() {
            std::error_code error;
            std::filesystem::remove_all(path, error);
        }
    } cleanup{work};

    for (bool sound : {true, false}) {
        const std::string reference = sound ? "patched:CarSettings" : "patched:LY_Multimedia";
        Json definition = {
                {"id", "test-passion"},
                {"platform", "passion"},
                {"board", "8155"},
                {"patches",
                 {{sound ? "CarSettings" : "LY_Multimedia",
                   {{"steps", Json::array({sound ? "passion-api30-settings-sound.json"
                                                 : "passion-api30-usb-media-session.json"})}}}}}};
        const auto archive = work / (sound ? "sound.apk" : "usb.apk");
        auto hex = std::string("504b03041400000000000000000079841f62190000001900000"
                               "00c000000636c6173736573332e646578"
                               "73796e74686574696320636f6d70696c65642068656c7065725"
                               "04b01021400140000000000000000007984"
                               "1f6219000000190000000c00000000000000000000000000000"
                               "00000636c6173736573332e646578504b"
                               "050600000000010001003a000000430000000000");
        const std::string old = "636c6173736573332e646578";
        const std::string replacement = sound ? "636c6173736573372e646578"
                                              : "636c6173736573342e646578";

        for (auto pos = hex.find(old); pos != std::string::npos;
             pos = hex.find(old, pos + replacement.size())) {
            hex.replace(pos, old.size(), replacement);
        }

        {
            std::ofstream out(archive, std::ios::binary);

            for (std::size_t i = 0; i < hex.size(); i += 2) {
                out.put(static_cast<char>(std::stoul(hex.substr(i, 2), nullptr, 16)));
            }

            assert(bool(out));
        }
        Json descriptor = {{"file", archive.string()},
                           {"sha256", sha256_file(archive)},
                           {"bytes", std::filesystem::file_size(archive)},
                           {"generated_helper",
                            {{"contract", sound ? "passion-settings-sound-helper-v1"
                                                : "passion-usb-media-session-helper-v1"},
                             {"artifact", reference},
                             {"archive_sha256", sha256_file(archive)},
                             {"entry", sound ? "classes7.dex" : "classes4.dex"},
                             {"sha256", sha256_bytes("synthetic compiled helper")},
                             {"bytes", std::uint64_t{25}}}}};
        const Json artifacts = {{reference, descriptor}};
        passion_generated_helper_graph(root, definition, artifacts);
        auto absent = definition;
        absent["patches"] = Json::object();
        passion_generated_helper_graph(root, absent, Json::object());
        rejects([&] {
            passion_generated_helper_graph(root, absent, artifacts);
        });
        auto foreign = definition;
        foreign["platform"] = "qinggan11";
        rejects([&] {
            passion_generated_helper_graph(root, foreign, artifacts);
        });
        rejects([&] {
            passion_generated_helper_graph(root, definition, Json::object());
        });

        for (const auto *field :
             {"contract", "artifact", "archive_sha256", "entry", "sha256", "bytes"}) {
            auto bad = artifacts;
            bad[reference]["generated_helper"].erase(field);
            rejects([&] {
                passion_generated_helper_graph(root, definition, bad);
            });
        }

        for (int mode = 0; mode < 8; ++mode) {
            auto bad = artifacts;
            auto &item = bad[reference];
            auto &helper = item["generated_helper"];

            if (mode == 0) {
                helper["sha256"] = std::string(64, '0');
            }

            if (mode == 1) {
                helper["archive_sha256"] = std::string(64, '0');
            }

            if (mode == 2) {
                helper["entry"] = "classes.dex";
            }

            if (mode == 3) {
                helper["bytes"] = std::uint64_t{26};
            }

            if (mode == 4) {
                helper["artifact"] = "patched:unrelated";
            }

            if (mode == 5) {
                helper["extra"] = true;
            }

            if (mode == 6) {
                item["bytes"] = std::uint64_t{0};
            }

            if (mode == 7) {
                item["extra"] = true;
            }

            rejects([&] {
                passion_generated_helper_graph(root, definition, bad);
            });
        }

        // The patched LY_Multimedia stores its entries and exceeds 64 MiB; an archive of that
        // size with the same helper entry is accepted.
        if (!sound) {
            std::vector<ZipAddition> padding;
            std::uint32_t state = 1;

            for (const auto *name :
                 {"assets/padding-1.bin", "assets/padding-2.bin", "assets/padding-3.bin"}) {
                std::string bytes(22ULL << 20, '\0');

                for (auto &byte : bytes) {
                    state = state * 1664525U + 1013904223U;
                    byte = static_cast<char>(state >> 24);
                }

                padding.push_back({name, bytes, sha256_bytes(bytes)});
            }

            const auto large = work / "usb-large.apk";
            rewrite_pinned_zip(archive, sha256_file(archive), {}, {}, padding, large, 1ULL << 30);
            assert(std::filesystem::file_size(large) > (64ULL << 20));
            auto item = descriptor;
            item["file"] = large.string();
            item["sha256"] = sha256_file(large);
            item["bytes"] = std::filesystem::file_size(large);
            item["generated_helper"]["archive_sha256"] = item["sha256"];
            passion_generated_helper_graph(root, definition, {{reference, item}});
        }
    }

    std::cout << "PASS generated helper actual archive binding and graph rejection\n";
} catch (const std::exception &error) {
    std::cerr << error.what() << '\n';

    return 1;
}
