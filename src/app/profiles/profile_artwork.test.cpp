#include "profile_artwork.hpp"
#include "control_artwork.hpp"
#include "portable_runtime.hpp"
#include "process.hpp"
#include "profile_runtime.hpp"
#include <fstream>
#include <iostream>
using namespace voyemu;
namespace {
void need(bool value, const char *message) {
    if (!value) {
        throw std::runtime_error(message);
    }
}

void put(const std::filesystem::path &path, const std::string &text) {
    ensure_private_directory(path.parent_path());
    std::ofstream out(path, std::ios::binary);
    out << text;
    need(bool(out), "fixture write failed");
}

std::string archive(const std::string &name, const std::string &bytes) {
    std::uint32_t crc = ~0u;

    for (unsigned char c : bytes) {
        crc ^= c;

        for (unsigned bit = 0; bit < 8; ++bit) {
            crc = (crc >> 1) ^ ((crc & 1) ? 0xedb88320u : 0);
        }
    }

    crc = ~crc;
    std::string out;
    auto n = [&](std::uint32_t value, unsigned size) {
        for (unsigned i = 0; i < size; ++i) {
            out += static_cast<char>(value >> (i * 8));
        }
    };
    n(0x04034b50, 4);
    n(20, 2);
    n(0, 2);
    n(0, 2);
    n(0, 4);
    n(crc, 4);
    n(bytes.size(), 4);
    n(bytes.size(), 4);
    n(name.size(), 2);
    n(0, 2);
    out += name;
    out += bytes;
    const auto central = out.size();
    n(0x02014b50, 4);
    n(20, 2);
    n(20, 2);
    n(0, 2);
    n(0, 2);
    n(0, 4);
    n(crc, 4);
    n(bytes.size(), 4);
    n(bytes.size(), 4);
    n(name.size(), 2);
    n(0, 2);
    n(0, 2);
    n(0, 2);
    n(0, 2);
    n(0, 4);
    n(0, 4);
    out += name;
    const auto length = out.size() - central;
    n(0x06054b50, 4);
    n(0, 2);
    n(0, 2);
    n(1, 2);
    n(1, 2);
    n(length, 4);
    n(central, 4);
    n(0, 2);

    return out;
}
} // namespace

int main(int argc, char **argv) {
    try {
        if (argc == 4 && std::string_view(argv[1]) == "--qinggan11-731-oem") {
            const auto owner = std::filesystem::canonical(argv[2]);
            const auto descriptor = read_private_json(std::filesystem::canonical(argv[3]));
            const std::string profile = "free-11-chinese-20260731-7.3.1";
            const auto root = owner / ".local/build/tmp" /
                              ("qinggan11-731-artwork-proof-" + random_token());
            ensure_private_directory(root);
            struct Cleanup {
                std::filesystem::path p;

                ~Cleanup() {
                    std::error_code error;
                    std::filesystem::remove_all(p, error);
                }
            } cleanup{root};
            const auto common = root / "profiles/common/wheels/free-2024";
            write_private_json(root / "profiles" / profile / "profile.json",
                               {{"wheel", {{"assets", "profiles/common/wheels/free-2024"}}}});
            write_private_json(common / "control-artwork.json", descriptor);
            std::map<std::filesystem::path, std::string> originals;

            for (const auto &input : descriptor.at("inputs")) {
                const auto source = owner / ".local/profiles" / profile /
                                    input.at("path").get<std::string>();
                need(sha256_file(source) == input.at("sha256").get<std::string>(),
                     "real A11 owner APK differs");
                const auto destination = root / ".local/profiles" / profile /
                                         input.at("path").get<std::string>();
                ensure_private_directory(destination.parent_path());
                std::filesystem::create_hard_link(source, destination);
                originals[source] = sha256_file(source);
            }

            const auto destination = root / "machines" / profile / "ui/controls";
            const auto files = export_profile_artwork(root, profile, destination);
            need(files.size() == 8, "seven OEM PNGs and provenance required");

            for (const auto &asset : descriptor.at("assets")) {
                need(sha256_file(destination / asset.at("file").get<std::string>()) ==
                             asset.at("png_sha256").get<std::string>(),
                     "real A11 exported PNG differs");
            }

            for (const auto &[source, pin] : originals) {
                need(sha256_file(source) == pin, "OEM original changed");
            }

            auto malformed = descriptor;
            malformed["assets"][0]["dimensions"][0] = 1;
            write_private_json(common / "control-artwork.json", malformed);
            bool denied = false;
            try {
                export_profile_artwork(root, profile, root / "machines/bad/ui/controls");
            } catch (...) {
                denied = true;
            }

            need(denied && !std::filesystem::exists(root / "machines/bad/ui/controls"),
                 "bad actual PNG dimensions published");
            std::cout
                    << "A11 four pinned owner APKs shared without copies; seven actual OEM PNG exports/dimensions and wrong-dimension rejection passed; no machine launched\n";

            return 0;
        }

        if (argc == 4 && std::string_view(argv[1]) == "--export-owner") {
            const auto root = std::filesystem::canonical(argv[2]);
            const std::string profile = argv[3];
            auto manifest = inspect_portable_runtime(root, profile,
                                                     voyemu::machine_identity(profile, {}));
            need(!manifest.contains("ui"), "owner UI already exported");
            const auto seed = root / "machines" / profile;
            const auto old = sha256_file(seed / "runtime.json");
            const auto files = export_profile_artwork(root, profile, seed / "ui/controls");
            need(sha256_file(seed / "runtime.json") == old, "runtime changed during export");
            manifest["ui"] = files;
            write_private_json(seed / "runtime.json", manifest);
            need(inspect_portable_runtime(root, profile, voyemu::machine_identity(profile, {})) ==
                         manifest,
                 "exported UI manifest rejected");
            std::cout << Json{{"files", files},
                              {"runtime_sha256", sha256_file(seed / "runtime.json")},
                              {"guest_images_changed", false},
                              {"qemu_started", false}}
                                 .dump()
                      << '\n';

            return 0;
        }

        need(argc == 1, "unexpected test arguments");
        const auto root = std::filesystem::canonical(std::filesystem::temp_directory_path()) /
                          ("voyemu-artwork-" + random_token());
        ensure_private_directory(root);
        struct Cleanup {
            std::filesystem::path p;

            ~Cleanup() {
                std::error_code e;
                std::filesystem::remove_all(p, e);
            }
        } cleanup{root};
        const std::string profile = "own-test", resource = "own PNG fixture",
                          member = "res/control.png";
        const auto apk = archive(member, resource);
        const auto common = root / "profiles/common/wheels/test";
        write_private_json(root / "profiles" / profile / "profile.json",
                           {{"wheel", {{"assets", "profiles/common/wheels/test"}}}});
        put(root / ".local/profiles" / profile / "originals/Own.apk", apk);
        Json spec = {{"schema", 1},
                     {"inputs",
                      {{"own", {{"path", "originals/Own.apk"}, {"sha256", sha256_bytes(apk)}}}}},
                     {"assets", Json::array({{{"file", "control.png"},
                                              {"input", "own"},
                                              {"member", member},
                                              {"png_sha256", sha256_bytes(resource)}}})}};
        write_private_json(common / "control-artwork.json", spec);
        const auto output = root / "machines" / profile / "ui/controls";
        const auto files = export_profile_artwork(root, profile, output);
        need(files.size() == 2 && sha256_file(output / "control.png") == sha256_bytes(resource),
             "owner PNG extraction differs");
        need(cluster::control_artwork_directory(common, profile) == output,
             "renderer did not select machine artwork");
        bool refused = false;
        try {
            export_profile_artwork(root, profile, output);
        } catch (...) {
            refused = true;
        }

        need(refused, "existing artwork overwritten");
        spec["assets"][0]["png_sha256"] = std::string(64, '0');
        write_private_json(common / "control-artwork.json", spec);
        refused = false;
        const auto absent = root / "machines" / profile / "bad";
        try {
            export_profile_artwork(root, profile, absent);
        } catch (...) {
            refused = true;
        }

        need(refused && !std::filesystem::exists(absent), "bad owner PNG pin published output");
        spec["assets"][0]["png_sha256"] = sha256_bytes(resource);
        spec["ownerProfiles"] = Json::array({profile});
        write_private_json(common / "control-artwork.json", spec);
        const auto allowed = root / "machines/allowed/ui/controls";
        const auto allowed_files = export_profile_artwork(root, profile, allowed);
        need(allowed_files.at("control.png") == files.at("control.png") &&
                     sha256_file(allowed / "control.png") == sha256_file(output / "control.png"),
             "owner scoping changed pinned artwork bytes");
        auto owner_definition = read_private_json(root / "profiles" / profile / "profile.json");
        owner_definition["original"] = {
                {"own", {{"file", "Own.apk"}, {"sha256", sha256_bytes(apk)}}}};
        write_private_json(root / "profiles" / profile / "profile.json", owner_definition);
        const std::string clone = "independent-owner-copy";
        auto clone_definition = owner_definition;
        clone_definition["id"] = clone;
        clone_definition["capture"] = {{"profile", clone}, {"referenceProfile", profile}};
        write_private_json(root / "profiles" / clone / "profile.json", clone_definition);
        const auto clone_output = root / "machines" / clone / "ui/controls";
        const auto clone_files = export_profile_artwork(root, clone, clone_output);
        need(clone_files == allowed_files &&
                     sha256_file(clone_output / "control.png") ==
                             sha256_file(output / "control.png") &&
                     !std::filesystem::exists(root / ".local/profiles" / clone / "originals"),
             "independent profile lost canonical owner artwork or copied originals");
        const std::string nxp = "free-09-chinese-20221230-nxp-rc45";
        write_private_json(root / "profiles" / nxp / "profile.json",
                           {{"wheel", {{"assets", "profiles/common/wheels/test"}}}});
        const auto skipped = root / "machines" / nxp / "ui/controls";
        need(export_profile_artwork(root, nxp, skipped).empty() &&
                     !std::filesystem::exists(skipped),
             "unsupported profile inherited owner artwork");

        for (const auto &bad : Json::array({Json::array(), Json::array({"../unsafe"}),
                                            Json::array({profile, profile}), Json::array({1})})) {
            spec["ownerProfiles"] = bad;
            write_private_json(common / "control-artwork.json", spec);
            bool denied = false;
            try {
                export_profile_artwork(root, nxp, skipped);
            } catch (...) {
                denied = true;
            }

            need(denied && !std::filesystem::exists(skipped),
                 "malformed owner profile scope accepted");
        }

        std::cout
                << "owner artwork extraction, exact pins, private machine location and nonreplacement passed\n";

        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';

        return 1;
    }
}
