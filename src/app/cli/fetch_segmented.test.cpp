#include "fetch_segmented.hpp"
#include "cli_messages.hpp"
#include "ext4_fixture.hpp"
#include "os/platform.hpp"
#include "process.hpp"
#include "progress.hpp"
#include "qinggan11_startup.hpp"
#include "qnx_qinggan11.hpp"
#include "quickboot.hpp"
#include "startup_animation_fixture.hpp"
#include <array>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif
using namespace voyemu;

static void check(bool value, const char *why) {
    if (!value) {
        throw std::runtime_error(why);
    }
}

// Whether text holds the lines in this order.
static bool ordered(const std::string &text, const std::vector<std::string> &lines) {
    std::size_t at = 0;

    for (const auto &line : lines) {
        at = text.find(line, at);

        if (at == std::string::npos) {
            return false;
        }

        at += line.size();
    }

    return true;
}

namespace {
// The pinned codec of this host, or the system zstd library on Linux; empty when neither exists.
std::filesystem::path zstd_library(const std::filesystem::path &project) {
    const auto manifest = read_private_json(project / "src/app/toolchain.json", 256 * 1024)
                                  .at("qnxUpdateCodec")
                                  .at(std::string(os::dependency_system));
    const auto pinned = project / manifest.at("file").get<std::string>();

    if (std::filesystem::is_regular_file(pinned)) {
        return std::filesystem::canonical(pinned);
    }

#ifdef __linux__
    if (void *handle = dlopen("libzstd.so.1", RTLD_NOW | RTLD_LOCAL)) {
        Dl_info info{};

        if (dladdr(dlsym(handle, "ZSTD_compress"), &info) && info.dli_fname) {
            return std::filesystem::canonical(info.dli_fname);
        }
    }
#endif

    return {};
}

// A synthetic segmented owner update: segmented system and resc images, four QNX images and a role
// without an update source, imported through the shared Free update route.
int segmented_synthetic_import(const std::filesystem::path &project) {
    const auto library = zstd_library(project);

    if (library.empty()) {
        std::cout << "No zstd codec on this host; run voyemu deps\n";

        return 77;
    }

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
    const auto compress =
            reinterpret_cast<std::size_t (*)(void *, std::size_t, const void *, std::size_t, int)>(
                    symbol("ZSTD_compress"));
    const auto is_error = reinterpret_cast<unsigned (*)(std::size_t)>(symbol("ZSTD_isError"));
    check(compress && is_error, "codec symbols");
    auto packed = [&](const std::string &bytes) {
        std::string output(bytes.size() + 1024, '\0');
        const auto size = compress(output.data(), output.size(), bytes.data(), bytes.size(), 3);
        check(!is_error(size), "fixture compression");
        output.resize(size);

        return output;
    };
    ensure_private_directory(project / ".local/build/tmp");
    const auto root = project / ".local/build/tmp" / ("segmented-update-" + random_token());
    check(std::filesystem::create_directory(root), "fresh fixture root");
    struct Cleanup {
        std::filesystem::path path;

        ~Cleanup() {
            std::error_code ignored;
            std::filesystem::remove_all(path, ignored);
        }
    } cleanup{root};
    const std::string profile = "test-qinggan09-1.0.0";
    const auto codec = root / ".local/toolchains/codec" / library.filename();
    ensure_private_directory(codec.parent_path());
    std::filesystem::copy_file(library, codec);
    const Json codec_pin = {{"file", codec.lexically_relative(root).generic_string()},
                            {"sha256", sha256_file(codec)}};
    write_private_json(root / "src/app/toolchain.json",
                       {{"qnxUpdateCodec",
                         {{"darwin", codec_pin}, {"linux", codec_pin}, {"win32", codec_pin}}}});

    const std::map<std::string, std::string> system_files = {
            {"/system/media/bootanimation.zip", "synthetic screen 1 animation"},
            {"/system/media/bootanimation1.zip", "synthetic screen 2 animation"},
            {"/system/media/bootanimation2.zip", "synthetic rear screen animation"},
            {"/system/priv-app/One/One.apk", "synthetic package"}};
    const std::map<std::string, std::string> resc_files = {
            {"/defaulttheme/simple/description.xml", "<theme/>"}};
    const auto system = ext4_fixture::build(system_files), resc = ext4_fixture::build(resc_files);
    std::vector<std::pair<std::string, std::string>> members;
    Json images = Json::object();
    auto image = [&](const std::string &name, const std::string &bytes, unsigned segments) {
        Json entries = Json::array(), sizes = Json::array();
        const auto segment = bytes.size() / segments;

        for (unsigned i = 0; i < segments; ++i) {
            const auto member = "android/la_" + name + "/la_" + name + ".patch" + std::to_string(i);
            members.emplace_back(member, packed(bytes.substr(i * segment, segment)));
            entries.push_back(member);
            sizes.push_back(segment);
        }

        images[name] = {{"file", "la_" + name + ".img"},
                        {"bytes", bytes.size()},
                        {"sha256", sha256_bytes(bytes)},
                        {"update",
                         {{"kind", "zero-base-zstd-segments"},
                          {"entries", entries},
                          {"segment_bytes", sizes}}}};
    };
    image("system", system, 2);
    image("resc", resc, 1);
    Json qnx_originals = Json::object();

    for (const auto *name : {"hyp_la", "ifs2_la", "system_la", "ifs2_recovery_la"}) {
        const auto bytes = std::string(4096, name[0]) + name;
        const auto member = std::string("qnx/") + name + "/" + name + ".patch0";
        members.emplace_back(member, packed(bytes));
        qnx_originals[std::string(name) + ".img"] = {
                {"file", std::string(name) + ".img"},
                {"bytes", bytes.size()},
                {"sha256", sha256_bytes(bytes)},
                {"update",
                 {{"kind", "zero-base-zstd-segments"},
                  {"entries", Json::array({member})},
                  {"segment_bytes", Json::array({bytes.size()})}}}};
    }

    members.emplace_back("mcu/update-mcu.zip", "unused member");
    const auto update = root / "update.zip";
    startup_fixture::zip(update, members);
    auto file = [](const std::string &name, const std::string &path, const std::string &bytes) {
        return Json{{"file", name},
                    {"bytes", bytes.size()},
                    {"sha256", sha256_bytes(bytes)},
                    {"source", {{"path", path}}},
                    {"acquisition", {{"image", "system"}, {"path", path}}}};
    };
    Json original = {
            {"BootAnimation", file("media/bootanimation.zip", "/system/media/bootanimation.zip",
                                   system_files.at("/system/media/bootanimation.zip"))},
            {"BootAnimation1", file("media/bootanimation1.zip", "/system/media/bootanimation1.zip",
                                    system_files.at("/system/media/bootanimation1.zip"))},
            {"One", file("One.apk", "/system/priv-app/One/One.apk", "synthetic package")},
            {"theme",
             {{"file", "theme/defaulttheme"},
              {"type", "tree"},
              {"source", {{"path", "/resc/defaulttheme"}}},
              {"sha256", {{"simple/description.xml", sha256_bytes("<theme/>")}}},
              {"bytes", {{"simple/description.xml", 8u}}},
              {"acquisition", {{"image", "resc"}, {"path", "/resc/defaulttheme"}}}}}};
    original["One"]["source"] = {{"package", "com.example.one"}};
    const Json owner = {{"file", "update.zip"},
                        {"bytes", std::filesystem::file_size(update)},
                        {"sha256", sha256_file(update)}};
    const auto directory = root / "profiles" / profile;
    write_private_json(directory / "profile.json",
                       {{"schema", 1},
                        {"original", original},
                        {"ownerUpdate", owner},
                        {"acquisition",
                         {{"format", "pinned-zip-zero-base-zstd-segments"},
                          {"diff", false},
                          {"qnxOriginals", Json::array({"hyp_la.img", "ifs2_la.img",
                                                        "system_la.img", "ifs2_recovery_la.img"})},
                          {"images", images}}}});
    write_private_json(directory / "profile-qnx.json",
                       {{"schema", 1},
                        {"update", {{"sha256", owner.at("sha256")}, {"source", "owner-update"}}},
                        {"originals", qnx_originals}});
    const auto definition = read_private_json(directory / "profile.json"),
               qnx = read_private_json(directory / "profile-qnx.json");
    check(segmented_acquisition_plan(definition, qnx).at("originals").size() == 4,
          "segmented plan differs");

    for (unsigned fault = 0; fault < 7; ++fault) {
        auto bad = definition, badq = qnx;

        if (fault == 0) {
            bad["acquisition"]["images"]["system"]["update"]["segment_bytes"][0] = 1u;
        }

        if (fault == 1) {
            bad["acquisition"]["images"]["system"]["update"]["entries"][1] =
                    "android/la_system/la_system.patch0";
        }

        if (fault == 2) {
            bad["acquisition"]["images"]["modem"] = bad["acquisition"]["images"]["resc"];
        }

        if (fault == 3) {
            badq["update"]["sha256"] = std::string(64, '0');
        }

        // A declared QNX original missing from profile-qnx.json, an undeclared one and a repeated
        // declaration.
        if (fault == 4) {
            badq["originals"].erase("ifs2_recovery_la.img");
        }

        if (fault == 5) {
            bad["acquisition"]["qnxOriginals"].erase(std::size_t{3});
        }

        if (fault == 6) {
            bad["acquisition"]["qnxOriginals"][3] = "hyp_la.img";
        }

        bool denied = false;
        try {
            segmented_acquisition_plan(bad, badq);
        } catch (const std::exception &) {
            denied = true;
        }

        check(denied, "invalid segmented plan accepted");
    }

    // Without deps the missing codec asks for voyemu deps instead of failing on the file.
    const auto codec_away = codec.string() + ".away";
    std::filesystem::rename(codec, codec_away);
    bool asked_for_deps = false;
    try {
        import_segmented_profile_inputs(root, profile, update, 0);
    } catch (const cli::LocalizedError &error) {
        asked_for_deps = error.code() == "FETCH_UPDATE" &&
                         error.render(cli::Language::english).find("Run: voyemu deps " + profile) !=
                                 std::string::npos;
    }

    check(asked_for_deps && !std::filesystem::exists(root / ".local/profiles" / profile),
          "fetch without deps did not ask for voyemu deps");
    std::filesystem::rename(codec_away, codec);
    // A missing archive names its path and the update it should be.
    bool named_missing = false;
    try {
        import_segmented_profile_inputs(root, profile, root / "updat.zip", 0);
    } catch (const cli::LocalizedError &error) {
        named_missing = error.code() == "FETCH_UPDATE" &&
                        error.render(cli::Language::english) ==
                                "FETCH_UPDATE: file " + (root / "updat.zip").string() +
                                        " not found.\nCheck the path to update.zip of the " +
                                        profile + " update.";
    }

    check(named_missing, "a missing archive did not name its path");
    // The console shows the import, the archive check, each image with its extracted files, the QNX
    // images and the saved originals; a rerun checks the archive and the retained originals only.
    std::ostringstream console;
    auto *prior = std::cerr.rdbuf(console.rdbuf());
    progress::console(cli::Language::english, false);
    Json imported;
    try {
        imported = import_segmented_profile_inputs(root, profile, update, 0);
    } catch (...) {
        std::cerr.rdbuf(prior);
        throw;
    }

    std::cerr.rdbuf(prior);
    check(ordered(console.str(),
                  {"Importing the " + profile + " update from " + update.string() + " (",
                   "\n\nChecking the archive\n",
                   "The archive matches the pinned " + profile + " update",
                   "\n\nAndroid system image (", "\n  Files extracted: 3",
                   "\n\nAndroid resc image (", "\n  Files extracted: 1", "\n\nQNX images (4)",
                   "\n\nSaving the originals (",
                   "\n\nOriginals saved: 4 Android files and 4 QNX images in "}),
          ("fresh import lines differ: " + console.str()).c_str());
    check(!imported.contains("profile_sha256") && !imported.contains("qnx_profile_sha256"),
          "segmented fetch must not pin its authoring profiles");
    const auto own = root / ".local/profiles" / profile;
    auto content = [](const std::filesystem::path &path) {
        std::ifstream source(path, std::ios::binary);

        return std::string(std::istreambuf_iterator<char>(source), {});
    };
    check(imported.at("cached") == false && imported.at("originals").size() == 4 &&
                  content(own / "originals/media/bootanimation.zip") ==
                          system_files.at("/system/media/bootanimation.zip") &&
                  content(own / "originals/media/bootanimation1.zip") ==
                          system_files.at("/system/media/bootanimation1.zip") &&
                  content(own / "originals/One.apk") == "synthetic package" &&
                  content(own / "originals/theme/defaulttheme/simple/description.xml") ==
                          "<theme/>",
          "segmented update originals differ");

    for (auto it = qnx_originals.begin(); it != qnx_originals.end(); ++it) {
        check(it.value().at("sha256") == sha256_file(own / "qnx" / it.key()),
              "segmented update QNX image differs");
    }

    console.str({});
    prior = std::cerr.rdbuf(console.rdbuf());
    Json rerun;
    try {
        rerun = import_segmented_profile_inputs(root, profile, update, 0);
    } catch (...) {
        std::cerr.rdbuf(prior);
        throw;
    }

    std::cerr.rdbuf(prior);
    check(rerun.at("cached") == true, "retained segmented inputs were imported again");
    check(ordered(console.str(), {"Checking the archive\n",
                                  "The archive matches the pinned " + profile + " update",
                                  "\n\nChecking the " + profile + " originals (",
                                  "\n" + profile +
                                          " originals are already imported, SHA-256 "
                                          "verified\n"}) &&
                  console.str().find("Importing") == std::string::npos,
          ("rerun lines differ: " + console.str()).c_str());
    // A profile that adds an original refreshes the retained cache with only the new file.
    const auto retained = cache_file_identity(own / "originals/One.apk");
    auto extended = definition;
    extended["original"]["BootAnimation2"] =
            file("media/bootanimation2.zip", "/system/media/bootanimation2.zip",
                 system_files.at("/system/media/bootanimation2.zip"));
    write_private_json(directory / "profile.json", extended);
    const auto refreshed = import_segmented_profile_inputs(root, profile, update, 0);
    check(!refreshed.contains("profile_sha256") && !refreshed.contains("qnx_profile_sha256"),
          "refreshed segmented inputs must not pin their authoring profiles");
    check(refreshed.at("cached") == false && refreshed.at("originals").size() == 5 &&
                  content(own / "originals/media/bootanimation2.zip") ==
                          system_files.at("/system/media/bootanimation2.zip") &&
                  cache_file_identity(own / "originals/One.apk") == retained,
          "an added original did not refresh the retained cache");
    check(import_segmented_profile_inputs(root, profile, update, 0).at("cached") == true,
          "the refreshed segmented cache was imported again");
    const auto pinned_bytes = std::filesystem::file_size(update);
    startup_fixture::zip(update, {{"foreign.bin", "foreign"}});
    bool denied = false;
    try {
        import_segmented_profile_inputs(root, profile, update, 0);
    } catch (const cli::LocalizedError &error) {
        const auto text = error.render(cli::Language::english);
        denied = error.code() == "FETCH_UPDATE" &&
                 text.starts_with("FETCH_UPDATE: " + update.string() + " is not the " + profile +
                                  " update: size ") &&
                 text.find(" bytes), expected " + std::to_string(pinned_bytes) +
                           " bytes.\nA full head unit update archive of version 1.0.0 "
                           "(update.zip) is needed.") != std::string::npos;
    }

    check(denied, "unpinned owner archive accepted without naming the expected update");
#ifdef _WIN32
    FreeLibrary(handle);
#else
    dlclose(handle);
#endif
    std::cout << "Synthetic segmented owner update: seven plan rejections, segmented system/resc "
                 "originals, four QNX images, cache reuse, cache refresh with an added original "
                 "and archive pin passed\n";

    return 0;
}
} // namespace

int main(int argc, char **argv) {
    try {
        if (argc == 3 && std::string_view(argv[1]) == "--segmented-synthetic") {
            return segmented_synthetic_import(std::filesystem::canonical(argv[2]));
        }

        if (argc == 5 && std::string_view(argv[1]) == "--import") {
            const auto result = import_segmented_profile_inputs(
                    std::filesystem::canonical(argv[2]), argv[3],
                    std::filesystem::canonical(argv[4]), 20ULL << 30);
            check(result.at("guest_started") == false && result.at("frame_verified") == false,
                  "segmented import receipt differs");
            std::cout << result.dump() << '\n';

            return 0;
        }

        Json qnx = {
                {"update",
                 {{"sha256", "a1b0c8e26eec79dcd738c140070ec7f31792dbafd63006d8be113c2bbdec4220"}}},
                {"hardware_adapter", "sa8155-qinggan11-731"},
                {"originals", Json::object()}};

        for (const auto &pin :
             qnx_qinggan11_adapter_originals(QnxGuestAdapter::sa8155_qinggan11_731)) {
            const auto base = std::string(pin.file).substr(0, std::string(pin.file).size() - 4);
            Json entries = Json::array(), sizes = Json::array();

            for (std::uint64_t at = 0, i = 0; at < pin.bytes; at += 512ULL << 20, ++i) {
                entries.push_back("qnx/" + base + "/" + base + ".patch" + std::to_string(i));
                sizes.push_back(std::min<std::uint64_t>(512ULL << 20, pin.bytes - at));
            }

            qnx["originals"][pin.file] = {{"file", pin.file},
                                          {"bytes", pin.bytes},
                                          {"sha256", pin.sha256},
                                          {"update",
                                           {{"kind", "zero-base-zstd-segments"},
                                            {"entries", entries},
                                            {"segment_bytes", sizes}}}};
        }

        Json images = Json::object();
        const std::array<const char *, 3>
                names = {"system", "vendor", "resc"},
                pins = {"c602475a9890bbe725a75ede7c8f667635fc1be69f9db65cae22e752c28fa3cd",
                        "9d729e9b8660222b686f3e8be3d91e01622537f3ca6b15d1d859f12a656e697b",
                        "cff851763db89a2e31377933467a38612203e4c716c589193053e1619d60dfba"};
        const std::array<unsigned, 3> counts = {8, 4, 6};

        for (unsigned role = 0; role < 3; ++role) {
            const std::string name = names[role];
            Json entries = Json::array(), sizes = Json::array();

            for (unsigned i = 0; i < counts[role]; ++i) {
                entries.push_back("android/la_" + name + "/la_" + name + ".patch" +
                                  std::to_string(i));
                sizes.push_back(536870912ULL);
            }

            images[name] = {{"file", "la_" + name + ".img"},
                            {"bytes", std::uint64_t(counts[role]) * (512ULL << 20)},
                            {"sha256", pins[role]},
                            {"update",
                             {{"kind", "zero-base-zstd-segments"},
                              {"entries", entries},
                              {"segment_bytes", sizes}}}};
        }

        const Json input = {
                {"file", "one.apk"},
                {"bytes", 1u},
                {"sha256", sha256_bytes("x")},
                {"source", {{"package", "com.example.fixture"}}},
                {"acquisition", {{"image", "system"}, {"path", "/system/app/one.apk"}}}};
        const Json tree = {
                {"file", "theme/defaulttheme"},
                {"type", "tree"},
                {"bytes", {{"one.xml", 1u}, {"two.xml", 2u}}},
                {"sha256", {{"one.xml", sha256_bytes("x")}, {"two.xml", sha256_bytes("xx")}}},
                {"source", {{"path", "/resc/defaulttheme"}}},
                {"acquisition", {{"image", "resc"}, {"path", "/resc/defaulttheme"}}}};
        const Json definition = {
                {"schema", 1},
                {"id", "test-qinggan11-a"},
                {"original", {{"one", input}, {"theme", tree}}},
                {"ownerUpdate",
                 {{"bytes", 4137925521ULL},
                  {"sha256", "a1b0c8e26eec79dcd738c140070ec7f31792dbafd63006d8be113c2bbdec4220"}}},
                {"acquisition",
                 {{"format", "pinned-zip-zero-base-zstd-segments"},
                  {"diff", false},
                  {"qnxOriginals", Json::array({"hyp_la.img", "ifs2_la.img", "system_la.img"})},
                  {"images", images}}}};
        const auto plan = segmented_acquisition_plan(definition, qnx);
        check(plan.at("originals").size() == 3 &&
                      plan.at("originals")[1].at("path") == "/resc/defaulttheme/one.xml",
              "file/tree source plan differs");
        auto renamed = definition;
        renamed["id"] = "test-qinggan11-b";
        check(segmented_acquisition_plan(renamed, qnx) == plan, "plan depends on the profile id");

        for (unsigned fault = 0; fault < 10; ++fault) {
            auto bad = definition, badq = qnx;

            if (fault == 0) {
                badq["hardware_adapter"] = "sa8155-qinggan11-611";
            }

            if (fault == 1) {
                bad["original"]["one"]["bytes"] = 1.0;
            }

            if (fault == 2) {
                bad["original"]["one"]["bytes"] = (512ULL << 20) + 1;
            }

            if (fault == 3) {
                bad["original"]["one"]["acquisition"]["path"] = "/system//one.apk";
            }

            if (fault == 4) {
                bad["original"]["one"]["acquisition"]["path"] = "/system/one.apk/";
            }

            if (fault == 5) {
                bad["original"]["one"]["acquisition"]["path"] = "/system/../one.apk";
            }

            if (fault == 6) {
                bad["acquisition"]["images"]["system"]["update"]["entries"][0] =
                        "android/la_system/la_system.patch1";
            }

            if (fault == 7) {
                bad["acquisition"]["images"]["system"]["update"]["segment_bytes"][0] = 536870912.0;
            }

            if (fault == 8) {
                badq["originals"]["hyp_la.img"]["file"] = "../foreign";
            }

            if (fault == 9) {
                bad["original"]["theme"]["bytes"].erase("two.xml");
            }

            bool denied = false;
            try {
                segmented_acquisition_plan(bad, badq);
            } catch (...) {
                denied = true;
            }

            check(denied, "invalid plan accepted");
        }

        auto older = definition, older_qnx = qnx;
        older_qnx["hardware_adapter"] = "sa8155-qinggan11-611";
        older["ownerUpdate"] = {
                {"bytes", 4226266936ULL},
                {"sha256", "ce6d1ffcd158c91383b61a44ea529bf54238512c520b436973fc0f6222047e42"}};
        older_qnx["update"]["sha256"] =
                "ce6d1ffcd158c91383b61a44ea529bf54238512c520b436973fc0f6222047e42";

        for (const auto &pin :
             qnx_qinggan11_adapter_originals(QnxGuestAdapter::sa8155_qinggan11_611)) {
            older_qnx["originals"][pin.file]["sha256"] = pin.sha256;
        }

        older["acquisition"]["images"]["system"]["sha256"] =
                "ee0c8241b7293b9cd9f936f622c556ac11ab3f92a0f60733e92dca1c1610f5c4";
        older["acquisition"]["images"]["vendor"]["sha256"] =
                "3043efb825b6ac90fb3380a84063c16de171bf812e4220200e40364612afdcba";
        older["acquisition"]["images"]["resc"]["sha256"] =
                "a5d0bc0c357d99c5f85277c13461c763f5a68bf9cf34ca71d425371b9d8decc2";
        check(segmented_acquisition_plan(older, older_qnx).at("originals").size() == 3,
              "second adapter acquisition rejected");

        for (unsigned fault = 0; fault < 8; ++fault) {
            auto bad = older, badq = older_qnx;

            if (fault == 0) {
                badq["hardware_adapter"] = "sa8155-qinggan11-731";
            }

            if (fault == 1) {
                bad["ownerUpdate"] = definition.at("ownerUpdate");
            }

            if (fault == 2) {
                bad["acquisition"]["images"]["system"]["sha256"] = "not-a-sha256";
            }

            if (fault == 3) {
                badq["originals"]["hyp_la.img"]["sha256"] =
                        qnx.at("originals").at("hyp_la.img").at("sha256");
            }

            if (fault == 4) {
                badq["update"]["sha256"] = qnx.at("update").at("sha256");
            }

            if (fault == 5) {
                badq["originals"]["system_la.img"]["update"]["segment_bytes"][0] = 1;
            }

            if (fault == 6) {
                bad["acquisition"]["images"]["modem"] = bad["acquisition"]["images"]["resc"];
            }

            if (fault == 7) {
                bad["ownerUpdate"]["bytes"] = 4226266936.0;
            }

            bool denied = false;
            try {
                segmented_acquisition_plan(bad, badq);
            } catch (...) {
                denied = true;
            }

            check(denied, "second adapter mixed or invalid acquisition accepted");
        }

        const Json qgapi = {
                {"file", "framework/QGAPI.jar"},
                {"sha256", "c178682be164be4872697dbb5accdc6ea28446941b6d681ddb736f2fdb627c07"},
                {"source", {{"path", "/system/framework/QGAPI.jar"}}}};
        check(qinggan11_731_qgapi_original(qgapi), "legacy QGAPI rejected");
        auto acquired = qgapi;
        acquired["bytes"] = 1968298u;
        acquired["acquisition"] = {{"image", "system"}, {"path", "/system/framework/QGAPI.jar"}};
        check(qinggan11_731_qgapi_original(acquired), "exact acquisition metadata rejected");

        for (unsigned fault = 0; fault < 5; ++fault) {
            auto bad = acquired;

            if (fault == 0) {
                bad["bytes"] = 1968298.0;
            }

            if (fault == 1) {
                bad["bytes"] = 1u;
            }

            if (fault == 2) {
                bad["acquisition"]["path"] = "/system/framework/foreign.jar";
            }

            if (fault == 3) {
                bad["extra"] = true;
            }

            if (fault == 4) {
                bad["sha256"] = std::string(64, '0');
            }

            check(!qinggan11_731_qgapi_original(bad), "QGAPI metadata widened exact donor");
        }

        std::cout
                << "qinggan11 segmented file/tree acquisition plan and 10 negative contracts passed\n";

        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';

        return 1;
    }
}
