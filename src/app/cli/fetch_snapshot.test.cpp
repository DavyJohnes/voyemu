#include "fetch_snapshot.hpp"
#include "ext4_fixture.hpp"
#include "fetch.hpp"
#include "process.hpp"
#include "startup_animation_fixture.hpp"
#include <iostream>
using namespace voyemu;

static void check(bool value, const char *why) {
    if (!value) {
        throw std::runtime_error(why);
    }
}

// A synthetic partition snapshot without QNX images imports the Android originals of a profile
// without profile-qnx.json through the owner update route of acquisition.format.
int main(int argc, char **argv) try {
    check(argc == 2, "project root required");
    const auto project = std::filesystem::canonical(argv[1]);
    ensure_private_directory(project / ".local/build/tmp");
    const auto root = project / ".local/build/tmp" / ("snapshot-update-" + random_token());
    check(std::filesystem::create_directory(root), "fresh fixture root");
    struct Cleanup {
        std::filesystem::path path;

        ~Cleanup() {
            std::error_code ignored;
            std::filesystem::remove_all(path, ignored);
        }
    } cleanup{root};
    const std::string profile = "test-snapshot", package = "synthetic package",
                      vendor_file = "<vendor/>";
    const auto system = ext4_fixture::build({{"/system/priv-app/One/One.apk", package}}),
               vendor = ext4_fixture::build({{"/etc/one.xml", vendor_file}}),
               vbmeta = std::string(4096, 'v');
    const auto snapshot = root / "snapshot.zip";
    startup_fixture::zip(snapshot,
                         {{"system.img", system}, {"vendor.img", vendor}, {"vbmeta.img", vbmeta}});
    auto image = [](const std::string &name, const std::string &bytes) {
        return Json{
                {"file", name + ".img"}, {"bytes", bytes.size()}, {"sha256", sha256_bytes(bytes)}};
    };
    const Json original = {
            {"One",
             {{"file", "One.apk"},
              {"bytes", package.size()},
              {"sha256", sha256_bytes(package)},
              {"source", {{"package", "com.example.one"}}},
              {"acquisition", {{"image", "system"}, {"path", "/system/priv-app/One/One.apk"}}}}},
            {"VendorOne",
             {{"file", "vendor/one.xml"},
              {"bytes", vendor_file.size()},
              {"sha256", sha256_bytes(vendor_file)},
              {"source", {{"path", "/vendor/etc/one.xml"}}},
              {"acquisition", {{"image", "vendor"}, {"path", "/vendor/etc/one.xml"}}}}}};
    const auto directory = root / "profiles" / profile;
    ensure_private_directory(directory);
    write_private_json(directory / "profile.json",
                       {{"schema", 1},
                        {"original", original},
                        {"acquisition",
                         {{"format", "pinned-snapshot-raw-images"},
                          {"diff", false},
                          {"archive",
                           {{"file", "snapshot.zip"},
                            {"bytes", std::filesystem::file_size(snapshot)},
                            {"sha256", sha256_file(snapshot)}}},
                          {"images",
                           {{"system", image("system", system)},
                            {"vendor", image("vendor", vendor)},
                            {"vbmeta", image("vbmeta", vbmeta)}}}}}});
    const auto imported = import_owner_profile_inputs(root, profile, snapshot, 0);
    const auto originals = root / ".local/profiles" / profile / "originals";
    check(imported.at("source") == "partition-snapshot" && imported.at("cached") == false &&
                  !imported.contains("qnx"),
          "snapshot import result differs");
    check(sha256_file(originals / "One.apk") == sha256_bytes(package) &&
                  sha256_file(originals / "vendor/one.xml") == sha256_bytes(vendor_file),
          "snapshot originals differ");
    check(!std::filesystem::exists(root / ".local/profiles" / profile / "qnx"),
          "snapshot import created QNX inputs");
    check(import_owner_profile_inputs(root, profile, snapshot, 0).at("cached") == true,
          "verified snapshot originals were not reused");
    std::cout << "Snapshot owner import without QNX PASS: system and vendor originals\n";

    return 0;
} catch (const std::exception &error) {
    std::cerr << error.what() << '\n';

    return 1;
}
