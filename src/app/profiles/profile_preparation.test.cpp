#include "profile_preparation.hpp"
#include "java_toolchain.hpp"
#include "machine_preparation.hpp"
#include "os/fs.hpp"
#include "process.hpp"
#include "profile_catalog.hpp"
#include "quickboot.hpp"
#include <algorithm>
#include <fstream>
#include <iostream>
#include <limits>
#include <set>
#include <utility>
using namespace voyemu;

// The bundled Android 9 profile on the 8155 board, read from the catalog.
static const std::string &qinggan09_profile() {
    static const auto id =
            embedded_profile_names(ProfilePlatform::qinggan09, ProfileBoard::sa8155).at(0);

    return id;
}

static void extraction_cache_checks(const std::filesystem::path &scratch) {
    const auto cache = scratch / "native-android-inputs";

    ensure_private_directory(cache);
    const std::string pin(64, 'a');
    const std::vector<std::string> all = {"/system/a.jar", "/system/b.jar"},
                                   subset = {"/system/a.jar"};
    Json identity = {{"image_sha256", pin}, {"paths", all}, {"logical_partition", "system"}};
    const auto directory = cache / sha256_bytes(identity.dump());
    ensure_private_directory(directory / "system");
    Json files = Json::object();

    for (const auto &path : all) {
        const auto file = directory / path.substr(1);
        std::ofstream(file) << path;
        files[path.substr(1)] = {{"bytes", std::filesystem::file_size(file)},
                                 {"sha256", sha256_file(file)}};
    }

    Json receipt = {{"schema", 1},
                    {"native", true},
                    {"filesystem_mounted", false},
                    {"image_sha256", pin},
                    {"logical_partition", "system"},
                    {"files", files}};
    const auto receipt_file = directory / "receipt.json";
    write_private_json(receipt_file, receipt);
    const auto receipt_pin = sha256_file(receipt_file);
    const auto receipt_identity = cache_file_identity(receipt_file);
    const auto extra = directory / "system/b.jar";
    const auto extra_pin = sha256_file(extra);
    const auto extra_identity = cache_file_identity(extra);

    if (cached_native_android_inputs(cache, pin, all, "system") != directory) {
        throw std::runtime_error("exact extraction cache miss");
    }

    if (cached_native_android_inputs(cache, pin, subset, "system") != directory) {
        throw std::runtime_error("verified superset extraction cache was not reused");
    }

    if (sha256_file(receipt_file) != receipt_pin ||
        cache_file_identity(receipt_file) != receipt_identity || sha256_file(extra) != extra_pin ||
        cache_file_identity(extra) != extra_identity ||
        std::distance(std::filesystem::directory_iterator(cache),
                      std::filesystem::directory_iterator{}) != 1) {
        throw std::runtime_error("cache selection rewrote or copied immutable inputs");
    }

    if (!cached_native_android_inputs(cache, std::string(64, 'b'), subset, "system").empty() ||
        !cached_native_android_inputs(cache, pin, subset, "system_ext").empty() ||
        !cached_native_android_inputs(cache, pin, {"/system/missing.jar"}, "system").empty()) {
        throw std::runtime_error("unrelated cache accepted");
    }

    auto rejects = [&](const std::function<void()> &operation) {
        bool denied = false;
        try {
            operation();
        } catch (...) {
            denied = true;
        }

        if (!denied) {
            throw std::runtime_error("unsafe extraction cache accepted");
        }
    };

    for (const auto &bad :
         std::vector<std::vector<std::string>>{{"system/a.jar"},
                                               {"/system/../outside"},
                                               {"/system/a.jar", "/system/a.jar"},
                                               {"/system/b.jar", "/system/a.jar"}}) {
        rejects([&] {
            cached_native_android_inputs(cache, pin, bad, "system");
        });
    }

    // All original files are verified, including files outside the requested subset.
    std::ofstream(extra) << "tampered extra";
    rejects([&] {
        cached_native_android_inputs(cache, pin, subset, "system");
    });
    std::ofstream(extra) << all[1];
    const auto outside = scratch / "outside.jar";
    std::ofstream(outside) << all[1];
    std::filesystem::remove(extra);
    std::filesystem::create_symlink(outside, extra);
    rejects([&] {
        cached_native_android_inputs(cache, pin, subset, "system");
    });
    std::filesystem::remove(extra);
    std::filesystem::create_hard_link(outside, extra);
    rejects([&] {
        cached_native_android_inputs(cache, pin, subset, "system");
    });
    std::filesystem::remove(extra);
    std::ofstream(extra) << all[1];
    const auto foreign = directory / "unknown";
    std::ofstream(foreign) << "unknown";
    rejects([&] {
        cached_native_android_inputs(cache, pin, subset, "system");
    });
    std::filesystem::remove(foreign);
    auto bad_receipt = receipt;
    bad_receipt["files"]["../outside"] = {{"bytes", 1}, {"sha256", pin}};
    write_private_json(receipt_file, bad_receipt);
    rejects([&] {
        cached_native_android_inputs(cache, pin, subset, "system");
    });
    write_private_json(receipt_file, receipt);
    const auto wrong_key = cache / std::string(64, 'f');
    std::filesystem::rename(directory, wrong_key);
    rejects([&] {
        cached_native_android_inputs(cache, pin, subset, "system");
    });
    std::filesystem::rename(wrong_key, directory);
    const auto exact_identity =
            Json{{"image_sha256", pin}, {"paths", subset}, {"logical_partition", "system"}};
    const auto exact = cache / sha256_bytes(exact_identity.dump());
    ensure_private_directory(exact);
    write_private_json(exact / "receipt.json", receipt);
    rejects([&] {
        cached_native_android_inputs(cache, pin, subset, "system");
    });
    std::filesystem::remove_all(exact);
    const auto receipt_copy = scratch / "original-receipt.json";
    write_private_json(receipt_copy, receipt);
    std::filesystem::remove(receipt_file);
    std::filesystem::create_symlink(receipt_copy, receipt_file);
    rejects([&] {
        cached_native_android_inputs(cache, pin, subset, "system");
    });
    std::filesystem::remove(receipt_file);
    std::filesystem::create_hard_link(receipt_copy, receipt_file);
    rejects([&] {
        cached_native_android_inputs(cache, pin, subset, "system");
    });
    std::filesystem::remove(receipt_file);
    write_private_json(receipt_file, receipt);
    const auto alias = cache / std::string(64, 'e');
    std::filesystem::create_symlink(directory, alias);
    rejects([&] {
        cached_native_android_inputs(cache, pin, subset, "system");
    });
    std::filesystem::remove(alias);
    const auto multiple = scratch / "multiple-caches";
    ensure_private_directory(multiple);
    auto add = [&](const std::vector<std::string> &inventory, const std::string &partition) {
        Json id = {{"image_sha256", pin}, {"paths", inventory}}, records = Json::object();

        if (!partition.empty()) {
            id["logical_partition"] = partition;
        }

        const auto result = multiple / sha256_bytes(id.dump());
        ensure_private_directory(result);

        for (const auto &path : inventory) {
            const auto file = result / path.substr(1);
            ensure_private_directory(file.parent_path());
            std::ofstream(file) << path;
            records[path.substr(1)] = {{"sha256", sha256_file(file)},
                                       {"bytes", std::filesystem::file_size(file)}};
        }

        Json original = {{"schema", 1},
                         {"native", true},
                         {"filesystem_mounted", false},
                         {"image_sha256", pin},
                         {"files", records}};

        if (!partition.empty()) {
            original["logical_partition"] = partition;
        }

        write_private_json(result / "receipt.json", original);

        return result;
    };
    const auto bigger = add({"/system/a.jar", "/system/b.jar", "/system/c.jar"}, "system"),
               first = add(all, "system"),
               second = add({"/system/a.jar", "/system/d.jar"}, "system");
    (void)bigger;
    const auto closest = std::min(first, second);

    if (cached_native_android_inputs(multiple, pin, subset, "system") != closest ||
        cached_native_android_inputs(multiple, pin, subset, "system") != closest) {
        throw std::runtime_error("superset selection was not minimal and deterministic");
    }

    const auto no_partition = add({"/vendor/a.so", "/vendor/b.so"}, "");

    if (cached_native_android_inputs(multiple, pin, {"/vendor/a.so"}) != no_partition ||
        cached_native_android_inputs(multiple, pin, {"/vendor/a.so", "/vendor/b.so"}) !=
                no_partition) {
        throw std::runtime_error("A9/vendor cache without logical partition was not reused");
    }

    if (cached_native_android_inputs(cache, pin, subset, "system") != directory ||
        sha256_file(receipt_file) != receipt_pin) {
        throw std::runtime_error("validated cache failed after negatives");
    }
}

static void native_executable_reserve_checks(const std::filesystem::path &root) {
    const std::string id = "free-11-chinese-20250710-6.11.1";
    const auto profile = read_private_json(root / "profiles" / id / "profile.json", 1 << 20);
    const auto exact = native_profile_executable_reserve(profile, id);

    if (exact != (8ULL << 30)) {
        throw std::runtime_error("Exact 611 executable reserve differs");
    }

    for (unsigned mode = 0; mode < 6; ++mode) {
        auto bad = profile;

        if (mode == 0) {
            bad["android"]["package"] = "other";
        }

        if (mode == 1) {
            bad["android"]["revision"] = "2";
        }

        if (mode == 2) {
            bad["android"]["sha256"] = std::string(64, '0');
        }

        if (mode == 3) {
            bad["android"]["url"] = "https://example.invalid/base.zip";
        }

        if (mode == 4) {
            bad["android"]["offline_selinux_labels"] = "other";
        }

        if (mode == 5) {
            bad["acquisition"]["images"]["system"]["sha256"] = std::string(64, '0');
        }

        if (native_profile_executable_reserve(bad, id) != (20ULL << 30)) {
            throw std::runtime_error("Altered 611 executable reserve accepted");
        }
    }

    for (const auto *ref : {"free-11-chinese-20260731-7.3.1", qinggan09_profile().c_str()}) {
        auto bad = profile;
        bad["capture"] = {{"profile", id}, {"referenceProfile", ref}};

        if (native_profile_executable_reserve(bad, id) != (20ULL << 30)) {
            throw std::runtime_error("Cross-profile executable reserve accepted");
        }
    }

    Json a9 = {{"android",
                {{"package", "system-images;android-28;default;arm64-v8a"},
                 {"revision", "1"},
                 {"sha256", "86844d2edf6ba7c7f11261f6be3fb37788c985cfcf3b8924cfe9f57a1ccbc0a9"}}}};

    if (native_profile_executable_reserve(a9, qinggan09_profile()) != (8ULL << 30)) {
        throw std::runtime_error("Exact A9 executable reserve differs");
    }

    const std::string wrapper = "free-09-chinese-20260106-6.0.3-copy";
    auto wrapped = a9;
    wrapped["id"] = wrapper;
    wrapped["capture"] = {{"profile", wrapper}, {"referenceProfile", qinggan09_profile()}};

    if (native_profile_executable_reserve(wrapped, wrapper) != (8ULL << 30)) {
        throw std::runtime_error("Exact A9 local copy reserve differs from A9");
    }

    wrapped["android"]["sha256"] = std::string(64, '0');

    if (native_profile_executable_reserve(wrapped, wrapper) != (20ULL << 30)) {
        throw std::runtime_error("Unpinned A9 local copy reserve accepted");
    }

    a9["android"]["revision"] = "2";

    if (native_profile_executable_reserve(a9, qinggan09_profile()) != (20ULL << 30)) {
        throw std::runtime_error("Altered A9 executable reserve accepted");
    }

    for (const auto *other :
         {"free-11-chinese-20260731-7.3.1", "passion-11-russian-20260403-2.1.4.80",
          qinggan09_profile().c_str(), "unknown"}) {
        if (native_profile_executable_reserve(Json{{"id", other}}, other) != (20ULL << 30)) {
            throw std::runtime_error("Non-611 executable reserve changed");
        }
    }
}

int main(int argc, char **argv) {
    try {
        if (argc == 2 && std::string_view(argv[1]) == "--native-executable-reserve-only") {
            const auto root = std::filesystem::path(__FILE__)
                                      .parent_path()
                                      .parent_path()
                                      .parent_path()
                                      .parent_path();
            native_executable_reserve_checks(root);
            std::cout << "Exact 611 native executable reserve and cross-profile fallback passed\n";

            return 0;
        }

        if (argc == 2 && std::string_view(argv[1]) == "--extraction-cache-only") {
            const auto scratch = std::filesystem::path(__FILE__)
                                         .parent_path()
                                         .parent_path()
                                         .parent_path()
                                         .parent_path() /
                                 ".local/build/tmp" /
                                 ("extraction-cache-regression-" + random_token());
            ensure_private_directory(scratch);
            struct Cleanup {
                std::filesystem::path path;

                ~Cleanup() {
                    std::error_code error;
                    std::filesystem::remove_all(path, error);
                }
            } cleanup{scratch};
            extraction_cache_checks(scratch);
            std::cout << "Immutable exact and superset extraction cache validation passed\n";

            return 0;
        }

        if (argc == 4 && std::string_view(argv[1]) == "--java-cache-only") {
            const auto root = std::filesystem::canonical(argv[2]);
            const auto helper =
                    ensure_java_preparation_tools(root, std::filesystem::canonical(argv[3]), true,
                                                  std::numeric_limits<std::uint64_t>::max());

            if (!helper.reused) {
                throw std::runtime_error("Java helper cache check allocated a new generation");
            }

            std::cout << helper.receipt.dump() << '\n';

            return 0;
        }

        if (argc == 5 && std::string_view(argv[1]) == "--prepare-artifacts") {
            const auto prepared = prepare_profile_artifacts(std::filesystem::canonical(argv[2]),
                                                            argv[3], true,
                                                            std::filesystem::canonical(argv[4]));
            std::cout << Json{{"result", prepared.result},
                              {"receipt", prepared.receipt},
                              {"work", prepared.work.string()},
                              {"transport", prepared.transport.string()},
                              {"guest_started", false}}
                                 .dump(2)
                      << '\n';

            return 0;
        }

        if (argc == 4 && std::string(argv[1]) == "--inputs") {
            std::cout << Json(native_profile_android_inputs(std::filesystem::canonical(argv[2]),
                                                            argv[3]))
                                 .dump()
                      << '\n';

            return 0;
        }

        if (argc != 1 && (argc != 3 || std::string_view(argv[1]) != "--root")) {
            throw std::runtime_error("usage: --inputs ROOT PROFILE | --root ABS_ROOT");
        }

        const auto source_root = argc == 3 ? std::filesystem::path(argv[2])
                                           : std::filesystem::path(__FILE__)
                                                     .parent_path()
                                                     .parent_path()
                                                     .parent_path()
                                                     .parent_path();

        if (argc == 3 && !source_root.is_absolute()) {
            throw std::runtime_error("test source root must be absolute");
        }

        const auto root = std::filesystem::canonical(source_root);

        if (!std::filesystem::is_directory(root / "profiles")) {
            throw std::runtime_error("test source root requires profiles");
        }

        for (const auto *id :
             {"free-09-chinese-20221230-nxp-rc45", "free-11-chinese-20260731-7.3.1",
              "passion-11-russian-20260403-2.1.4.80"}) {
            const auto profile = read_private_json(root / "profiles" / id / "profile.json",
                                                   1 << 20);

            if (android_candidate_reserve_bytes(profile) != (8ULL << 30)) {
                throw std::runtime_error("Registered profile Java helper caller reserve differs");
            }

            auto invalid = profile;
            invalid["android"]["package"] = "system-images;android-29;default;arm64-v8a";
            bool refused = false;
            try {
                android_candidate_reserve_bytes(invalid);
            } catch (...) {
                refused = true;
            }

            if (!refused) {
                throw std::runtime_error("Java helper profile reserve accepted an unpinned base");
            }
        }

        if (android_candidate_reserve_bytes(Json{{"id", "unknown"}}) != (20ULL << 30)) {
            throw std::runtime_error("Unregistered Java helper profile reserve changed");
        }

        const auto nxp = read_private_json(
                root / "profiles/free-09-chinese-20221230-nxp-rc45/profile.json", 1 << 20);

        if (native_profile_android_input_reserve(nxp) != (8ULL << 30)) {
            throw std::runtime_error("NXP extraction reserve differs");
        }

        for (const auto id : {qinggan09_profile().c_str(), "free-11-chinese-20260731-7.3.1",
                              "passion-11-russian-20260403-2.1.4.80", "unknown"}) {
            if (native_profile_android_input_reserve(Json{{"id", id}}) != (20ULL << 30)) {
                throw std::runtime_error("Other extraction reserve changed");
            }
        }

        {
            auto bad = nxp;
            bad["android"]["package"] = "system-images;android-29;default;arm64-v8a";
            bool refused = false;
            try {
                native_profile_android_input_reserve(bad);
            } catch (...) {
                refused = true;
            }

            if (!refused) {
                throw std::runtime_error("Unpinned NXP extraction base accepted");
            }
        }
        const auto paths = native_profile_android_inputs(root, qinggan09_profile());
        std::set<std::string> unique(paths.begin(), paths.end());

        if (paths.size() < 57 || paths.size() != unique.size() ||
            !unique.contains("/system/framework/framework-res.apk") ||
            !unique.contains("/system/lib64/libandroid_runtime.so") ||
            !unique.contains("/system/lib64/libstagefright_ccodec.so") ||
            !unique.contains("/vendor/etc/init/rild.rc") ||
            !unique.contains("/system/etc/selinux/plat_sepolicy.cil")) {
            throw std::runtime_error("profile Android extraction plan omitted required inputs");
        }

        for (const char *stem :
             {"core-oj", "core-libart", "conscrypt", "okhttp", "bouncycastle", "apache-xml", "ext",
              "framework", "telephony-common", "voip-common", "ims-common",
              "android.hidl.base-V1.0-java", "android.hidl.manager-V1.0-java",
              "framework-oahl-backward-compatibility", "android.test.base"}) {
            const std::string name = stem;
            const auto boot = name == "core-oj" ? std::string("boot") : "boot-" + name;

            for (const auto &input :
                 {"/system/framework/" + name + ".jar", "/system/framework/" + boot + ".vdex",
                  "/system/framework/arm64/" + boot + ".oat"}) {
                if (!unique.contains(input)) {
                    throw std::runtime_error("missing bootclasspath source: " + input);
                }
            }
        }

        for (const char *stem :
             {"services", "ethernet-service", "wifi-service", "com.android.location.provider"}) {
            const std::string name = stem;

            for (const auto &input : {"/system/framework/" + name + ".jar",
                                      "/system/framework/oat/arm64/" + name + ".odex",
                                      "/system/framework/oat/arm64/" + name + ".vdex"}) {
                if (!unique.contains(input)) {
                    throw std::runtime_error("missing system server source: " + input);
                }
            }
        }

        for (const auto &input :
             {"/system/framework/content.jar", "/system/framework/oat/arm64/content.odex",
              "/system/framework/oat/arm64/content.vdex"}) {
            if (!unique.contains(input)) {
                throw std::runtime_error("missing standalone content CLI source");
            }
        }

        bool refused = false;
        try {
            native_profile_android_inputs(root, "../outside");
        } catch (...) {
            refused = true;
        }

        if (!refused) {
            throw std::runtime_error("unsafe profile accepted");
        }

        {
            const Json idc_profile = read_private_json(
                    root / "profiles/free-09-chinese-20221230-nxp-rc45/profile.json", 1 << 20);
            const auto find_idc_install = [&](const Json &definition, const std::string &artifact,
                                              const char *target) {
                for (const auto &row : definition.at("install")) {
                    if (row.at("artifact").get_ref<const std::string &>() == artifact) {
                        if (row.at("type").get_ref<const std::string &>() != "file" ||
                            row.at("target").get_ref<const std::string &>() != target ||
                            row.at("mode").get_ref<const std::string &>() != "0644") {
                            throw std::runtime_error("NXP IDC install node shape differs: " +
                                                     artifact);
                        }

                        return true;
                    }
                }

                return false;
            };
            bool touch1 = find_idc_install(idc_profile, "files:idc-touch-1",
                                           "/system/usr/idc/virtio_input_multi_touch_1.idc"),
                 touch2 = find_idc_install(idc_profile, "files:idc-touch-2",
                                           "/system/usr/idc/virtio_input_multi_touch_2.idc");

            if (!touch1 || !touch2) {
                throw std::runtime_error(
                        "NXP profile must install both A9-parity virtio touch IDC nodes (type file, mode 0644)");
            }

            const auto idc_file_mapping = [&](const std::string &artifact,
                                              const char *expected_file) {
                const auto &files = idc_profile.at("files");

                if (!files.contains(artifact) || !files.at(artifact).is_object() ||
                    !files.at(artifact).contains("file") ||
                    files.at(artifact).at("file").get_ref<const std::string &>() != expected_file) {
                    throw std::runtime_error(
                            "NXP IDC profile file definition or resolved asset missing/wrong for " +
                            artifact);
                }
            };
            idc_file_mapping("idc-touch-1", "virtio_input_multi_touch_1.idc");
            idc_file_mapping("idc-touch-2", "virtio_input_multi_touch_2.idc");
            {
                std::ifstream in(
                        root / "profiles/free-09-chinese-20221230-nxp-rc45/files/virtio_input_multi_touch_2.idc",
                        std::ios::binary);
                const std::string idc2_bytes(std::istreambuf_iterator<char>(in), {});

                if (idc2_bytes.find(
                            "touch.displayId = virtual:com.android.emulator.multidisplay,1000,Emulator2D,0") ==
                    std::string::npos) {
                    throw std::runtime_error("NXP IDC _2 secondary-display binding differs");
                }
            }
        }
        refused = false;
        try {
            prepare_profile_artifacts("relative", qinggan09_profile(), true);
        } catch (...) {
            refused = true;
        }

        if (!refused) {
            throw std::runtime_error("relative root accepted by preparation adapter");
        }

        const auto fixture = root / ".local/build/tmp" /
                             ("qinggan11-profile-inputs-" + random_token());
        ensure_private_directory(fixture / "profiles/api30-fixture");
        struct Cleanup {
            std::filesystem::path path;

            ~Cleanup() {
                std::filesystem::remove_all(path);
            }
        } cleanup{fixture};
        extraction_cache_checks(fixture);
        auto api30 = read_private_json(
                root / "profiles/free-11-chinese-20250710-6.11.1/profile.json", 4 << 20);
        api30["android"]["revision"] = "1";
        api30["patches"] = Json::object();
        api30["components"] = Json::object();
        write_private_json(fixture / "profiles/api30-fixture/profile.json", api30);

        if (native_profile_android_inputs(fixture, "api30-fixture") !=
            std::vector<std::string>{"/system/framework/framework-res.apk"}) {
            throw std::runtime_error("minimal API30 profile acquired guessed Android inputs");
        }

        // The preparation lock belongs to its live process only: a running preparation refuses a
        // second one, while the file of an exited one stays behind without blocking the next
        // preparation, which here stops offline.
        const auto preparation_lock = fixture / ".local/build/prepare-api30-fixture.lock";
        const auto prepare_failure = [&] {
            try {
                prepare_profile_artifacts(fixture, "api30-fixture", true);
            } catch (const std::exception &error) {
                return std::string(error.what());
            }

            return std::string();
        };
        ensure_private_directory(preparation_lock.parent_path());
        {
            const os::FileLock running(preparation_lock, true);

            const auto beside_running = prepare_failure();

            if (!beside_running.starts_with("PROFILE_PREPARATION:")) {
                throw std::runtime_error("a second preparation ran beside a running one: " +
                                         beside_running);
            }
        }

        const auto after_exit = prepare_failure();

        if (after_exit.starts_with("PROFILE_PREPARATION:")) {
            throw std::runtime_error("the lock of an exited preparation blocked the next one: " +
                                     after_exit);
        }

        api30["patches"] = {{"stock-statusbar",
                             {{"input", "android:/system_ext/priv-app/SystemUI/SystemUI.apk"},
                              {"steps", Json::array()}}}};
        write_private_json(fixture / "profiles/api30-fixture/profile.json", api30);

        if (native_profile_android_inputs(fixture, "api30-fixture") !=
            std::vector<std::string>{"/system/framework/framework-res.apk",
                                     "/system_ext/priv-app/SystemUI/SystemUI.apk"}) {
            throw std::runtime_error(
                    "API30 stock SystemUI input must derive from SDK system_ext partition");
        }

        // A jar component lists framework files as guest libraries; only a native component also
        // reads the C and C++ runtime libraries, which API30 keeps as symbolic links to the APEX.
        ensure_private_directory(fixture / "profiles/api30-fixture/components/jar");
        write_private_json(fixture / "profiles/api30-fixture/components/jar/component.json",
                           {{"type", "jar"}, {"guestLibs", {"/system/framework/framework.jar"}}});
        api30["patches"] = Json::object();
        api30["components"] = {{"jar", "components/jar"}};
        write_private_json(fixture / "profiles/api30-fixture/profile.json", api30);

        if (native_profile_android_inputs(fixture, "api30-fixture") !=
            std::vector<std::string>{"/system/framework/framework-res.apk",
                                     "/system/framework/framework.jar"}) {
            throw std::runtime_error("jar component acquired native runtime libraries");
        }

        ensure_private_directory(fixture / "profiles/api30-fixture/components/native");
        write_private_json(fixture / "profiles/api30-fixture/components/native/component.json",
                           {{"type", "native"}, {"guestLibs", {"/system/lib64/liblog.so"}}});
        api30["components"] = {{"native", "components/native"}};
        write_private_json(fixture / "profiles/api30-fixture/profile.json", api30);

        if (native_profile_android_inputs(fixture, "api30-fixture") !=
            std::vector<std::string>{"/system/framework/framework-res.apk",
                                     "/system/lib64/libc++.so", "/system/lib64/libc.so",
                                     "/system/lib64/libdl.so", "/system/lib64/liblog.so",
                                     "/system/lib64/libm.so"}) {
            throw std::runtime_error("native component runtime libraries differ");
        }

        api30["components"] = Json::object();
        api30["patches"] = {{"stock-statusbar",
                             {{"input", "android:/system_ext/priv-app/SystemUI/SystemUI.apk"},
                              {"steps", Json::array()}}}};

        for (const auto *input : {"android:/system_ext/../outside",
                                  "android:/system_ext2/priv-app/SystemUI/SystemUI.apk",
                                  "android:/data/priv-app/SystemUI/SystemUI.apk"}) {
            auto invalid = api30;
            invalid["patches"]["stock-statusbar"]["input"] = input;
            write_private_json(fixture / "profiles/api30-fixture/profile.json", invalid);
            refused = false;
            try {
                native_profile_android_inputs(fixture, "api30-fixture");
            } catch (...) {
                refused = true;
            }

            if (!refused) {
                throw std::runtime_error("Unsafe stock SystemUI extraction path accepted");
            }
        }

        {
            auto invalid = api30;
            invalid["android"] =
                    read_private_json(root / "profiles" / qinggan09_profile() / "profile.json")
                            .at("android");
            write_private_json(fixture / "profiles/api30-fixture/profile.json", invalid);
            refused = false;
            try {
                native_profile_android_inputs(fixture, "api30-fixture");
            } catch (...) {
                refused = true;
            }

            if (!refused) {
                throw std::runtime_error("API28 system_ext input accepted");
            }
        }
        api30["patches"] = Json::object();
        api30["android"]["revision"] = "latest";
        write_private_json(fixture / "profiles/api30-fixture/profile.json", api30);
        refused = false;
        try {
            native_profile_android_inputs(fixture, "api30-fixture");
        } catch (...) {
            refused = true;
        }

        if (!refused) {
            throw std::runtime_error("API30 malformed revision accepted");
        }

        api30["android"]["revision"] = "1";
        api30["patches"] = {{"manifest",
                             {{"input", "original:SystemService"},
                              {"steps", Json::array({"libraries.json"})}}}};
        ensure_private_directory(fixture / "profiles/api30-fixture/patches");
        const auto recipe = fixture / "profiles/api30-fixture/patches/libraries.json";
        write_private_json(fixture / "profiles/api30-fixture/profile.json", api30);
        write_private_json(
                recipe,
                {{"schema", 1},
                 {"api", 30},
                 {"calls", Json::array({Json{{"operation", "useLibraries"},
                                             {"args", Json::array({Json::array({"QGAPI"})})}}})}});
        const auto api30_metadata = profile_patch_metadata(fixture, "api30-fixture");

        if (api30_metadata.at("libraries.json").at("api") != 30 ||
            api30_metadata.at("libraries.json").at("sourceSha256") != sha256_file(recipe)) {
            throw std::runtime_error("API30 manifest recipe rejected or unpinned");
        }

        for (unsigned wrong_api : {28u, 29u, 31u}) {
            auto wrong = read_private_json(recipe);
            wrong["api"] = wrong_api;
            write_private_json(recipe, wrong);
            refused = false;
            try {
                profile_patch_metadata(fixture, "api30-fixture");
            } catch (...) {
                refused = true;
            }

            if (!refused) {
                throw std::runtime_error("recipe API differs from registered clean base");
            }
        }

        const auto keymanager_calls = Json::parse(
                R"([{"operation":"guardKeyManager","args":[]},{"operation":"useLibraries","args":[["QGAPI","QGMOD","QGVehicle","VoyemuFreeWheel"]]}])");
        const auto passion_inputs =
                native_profile_android_inputs(root, "passion-11-russian-20260403-2.1.4.80");

        if (std::count(passion_inputs.begin(), passion_inputs.end(),
                       "/system/framework/services.jar") != 1) {
            throw std::runtime_error("Passion requires exactly one clean services JAR input");
        }

        const auto passion_metadata =
                profile_patch_metadata(root, "passion-11-russian-20260403-2.1.4.80");
        const auto passion_nav =
                root /
                "profiles/passion-11-russian-20260403-2.1.4.80/patches/passion-api30-secondary-navigation.json";

        if (passion_metadata.at("passion-api30-secondary-navigation.json").at("sourceSha256") !=
                    sha256_file(passion_nav) ||
            passion_metadata.at("passion-api30-secondary-navigation.json").at("calls") !=
                    Json::array({Json{{"operation", "guardSecondaryNavigationDecorated"},
                                      {"args", Json::array()}}})) {
            throw std::runtime_error("Passion clean policy recipe source and operation must match");
        }

        const auto qinggan11_metadata = profile_patch_metadata(root,
                                                               "free-11-chinese-20260731-7.3.1");
        const std::set<std::string> qinggan11_recipe_names = {
                "can-early-file-contexts-api30.json",
                "can-early-policy-api30.json",
                "can-early-property-contexts-api30.json",
                "can-simulator-contexts-api30.json",
                "can-simulator-policy-api30.json",
                "original-navi-egl.json",
                "original-navi-libraries.json",
                "original-navi-selinux.json",
                "original-navi-vehicle.json",
                "original-navi-vin.json",
                "usb-mount-visibility.json",
                "usb-port-metadata.json",
                "api30-audio-policy.json",
                "api30-audio-consumer.json",
                "api30-media-notification-actions.json",
                "api30-usb-player.json",
                "api30-gallery-arm64.json",
                "api30-dvr-arm64.json",
                "api30-dvr-camera-virtual.json",
                "api30-framework-libraries.json",
                "framework-libraries.json",
                "api30-aqy-provider.json",
                "api30-hiboard-libraries.json",
                "api30-hiboard-routing.json",
                "api30-hiboard-startup.json",
                "api30-phone-provider.json",
                "api30-key-ingress.json",
                "api30-keymanager.json",
                "api30-launcher-power.json",
                "api30-launcher-canbus.json",
                "api30-launcher-dvr.json",
                "api30-navigation-camera-libraries.json",
                "api30-camera-virtual.json",
                "api30-media-cloud-availability.json",
                "api30-media-runtime.json",
                "api30-music-fullscreen.json",
                "api30-music-page.json",
                "api30-network.json",
                "api30-secondary-navigation.json",
                "api30-str-power.json",
                "api30-settings-voice-clone.json",
                "api30-systempolicy-hardware.json",
                "api30-qgbus-libraries.json",
                "api30-systemui-routing.json",
                "api30-stock-statusbar.json",
                "api30-thirdscreen-rear.json",
                "api30-vehicle-libraries.json",
                "keymanager-contexts-api30.json",
                "property-contexts-api30.json",
                "music-optional-arm32.json",
                "public-qg-runtime-api30.json",
                "selinux-hash-api30.json",
                "selinux-services-api30.json"};
        std::set<std::string> actual_qinggan11_recipes;

        for (auto it = qinggan11_metadata.begin(); it != qinggan11_metadata.end(); ++it) {
            actual_qinggan11_recipes.insert(it.key());

            if (it.value().at("sourceSha256") !=
                sha256_file(root / "profiles/free-11-chinese-20260731-7.3.1/patches" / it.key())) {
                throw std::runtime_error("Free A11 recipe metadata not bound to actual source");
            }
        }

        if (actual_qinggan11_recipes != qinggan11_recipe_names) {
            throw std::runtime_error("Free A11 named recipe coverage differs");
        }

        if (qinggan11_metadata.at("api30-key-ingress.json").at("calls") !=
                    Json::array(
                            {Json{{"operation", "guardKeyIngress"}, {"args", Json::array()}}}) ||
            qinggan11_metadata.at("api30-secondary-navigation.json").at("calls") !=
                    Json::array(
                            {Json{{"operation", "guardSecondaryNavigationSuppressed"},
                                  {"args", Json::array()}},
                             Json{{"operation", "guardHiboardTopGesture"}, {"args", Json::array()}},
                             Json{{"operation", "guardStartupVolume"}, {"args", Json::array()}},
                             Json{{"operation", "guardInterguestStartup"},
                                  {"args", Json::array()}}}) ||
            qinggan11_metadata.at("api30-keymanager.json").at("calls") != keymanager_calls ||
            qinggan11_metadata.at("api30-systemui-routing.json").at("calls") !=
                    Json::array({Json{{"operation", "routeSystemUi"}, {"args", Json::array()}}}) ||
            qinggan11_metadata.at("api30-aqy-provider.json").at("calls") !=
                    Json::array(
                            {Json{{"operation", "guardAqyProvider"}, {"args", Json::array()}}}) ||
            qinggan11_metadata.at("api30-launcher-power.json").at("calls") !=
                    Json::array(
                            {Json{{"operation", "guardLauncherPower"}, {"args", Json::array()}}}) ||
            qinggan11_metadata.at("property-contexts-api30.json").at("output_sha256") !=
                    "50c13ef43f195b1b94c5e1ed85f3a33578220e6074d9d244414ae73c9837d2db" ||
            qinggan11_metadata.at("api30-str-power.json").at("calls") !=
                    Json::array({Json{{"operation", "guardStrPower"}, {"args", Json::array()}}}) ||
            qinggan11_metadata.at("framework-libraries.json").at("api") != 30 ||
            qinggan11_metadata.at("api30-network.json").at("calls") !=
                    Json::array({Json{{"operation", "guardNetwork"}, {"args", Json::array()}}}) ||
            qinggan11_metadata.at("public-qg-runtime-api30.json")
                            .at("constants")
                            .at("output_sha256") !=
                    "61307bd17de318871f19ed5effede9e65204d7c510e246d91155cc7162744763") {
            throw std::runtime_error("actual A11 manifest recipe coverage differs");
        }

        const auto media_calls = Json::parse(
                R"([{"operation":"useLibraries","args":[["QGAPI","QGMOD","QGVehicle"]]},{"operation":"removePinnedEntry","args":["lib/armeabi-v7a/lib2496p2p.so","6943090c8218e1b6f7d0708a29a73152d4b1fe3e572054730d48cc1981386f58"]},{"operation":"removePinnedEntry","args":["lib/armeabi-v7a/libQPlayer.so","707e76aa29560b2c5fc3604bf5335c73d62f7d890290c86c8c5cb5af0775fb45"]},{"operation":"removePinnedEntry","args":["lib/armeabi-v7a/libSuperSound3.so","f43acf5b564c0bd03879e747803fdadd36de0513103caaed6490b825ae103e36"]},{"operation":"removePinnedEntry","args":["lib/armeabi-v7a/libcrypto_1_1.so","1334f4a1c8e5f915b9f64ccad626d6120381c58d46a58158987c81e08780c6a3"]},{"operation":"removePinnedEntry","args":["lib/armeabi-v7a/libdeStream.so","377589d8ef02a2abbbc70f10c9a5a237690af71f4a3221cbf23db7731727dba0"]},{"operation":"removePinnedEntry","args":["lib/armeabi-v7a/libkaolafmffmpeg.so","32378f309c5018c71b80e182617d3317ff6cf38c7dffa23e312f7488cd0d7a78"]},{"operation":"removePinnedEntry","args":["lib/armeabi-v7a/libkaolafmplayer.so","cc2e50ddf92ae7ff1aaa5a052ea15380e6266e9dc1a250f5875b92c99ea1013a"]},{"operation":"removePinnedEntry","args":["lib/armeabi-v7a/libkaolafmsdl.so","1718fb8ee29cf88edec824116f678845e98ad72de1ee898ba49e59182b28d534"]},{"operation":"removePinnedEntry","args":["lib/armeabi-v7a/libkaolafmse.so","550c04692c53f480c3508121ed5919eb386b14f24072909df55d90a0da82b525"]},{"operation":"removePinnedEntry","args":["lib/armeabi-v7a/libkaolafmsoundtouch.so","40bd499d945d84df1ee6fa10436c624854bf8fe2cf00d61f4a9521b9b56b70c7"]},{"operation":"removePinnedEntry","args":["lib/armeabi-v7a/libkpk.so","35ab335e13a9e2a078293a0e8fde3b40161a6392e8fea1756bcee6adfde38fc2"]},{"operation":"removePinnedEntry","args":["lib/armeabi-v7a/libkwbase.so","2aefb0534e9f13a484fa35df3eef1bc93cecb79cd4750a56a88dfc32e5564c54"]},{"operation":"removePinnedEntry","args":["lib/armeabi-v7a/libkwffmpeg.so","1894ea5125f35a347b257f37d23698e63dd5fb29a690fb50dfd68ad0399827f8"]},{"operation":"removePinnedEntry","args":["lib/armeabi-v7a/libkwffutil.so","479a4a8b936cc2c6d2979f7c0840c733202f7ad30e2c7b29ae06cdbf46850de4"]},{"operation":"removePinnedEntry","args":["lib/armeabi-v7a/libkwinfodecode.so","fd2fa50f3279eb2b0dfbd5e066731d5f43457cd208b237e0ee92df9f51e88f71"]},{"operation":"removePinnedEntry","args":["lib/armeabi-v7a/libkwplayer.so","f8627e779a1089686280f6964325b14118e85268675ce1feaae235ded15f832d"]},{"operation":"removePinnedEntry","args":["lib/armeabi-v7a/libkwsdl.so","c08d686ee8d9e20122398863d4e293a1aa3ec9b12d50fd96eb39f2518f970113"]},{"operation":"removePinnedEntry","args":["lib/armeabi-v7a/libmusic3d.so","60ebbda464206422b1abe9449584e4901ec0943a72d87c11ee54fce6ee491be8"]},{"operation":"removePinnedEntry","args":["lib/armeabi-v7a/libopenal.so","9185b69272d8f8d9d10b471cc967207c88ff1da35134aa84a1d1b3dca2da9c71"]},{"operation":"removePinnedEntry","args":["lib/armeabi-v7a/libopenalwrapper.so","35c4a074dacdb158476e27d88e7ee6e4b0d193fc7dd9a281ec1166b2a1c8b4d1"]},{"operation":"removePinnedEntry","args":["lib/armeabi-v7a/libp2p.so","f0d8c889614fd25dd5351f9867ab865fb0197ed66a8fe7eaa563719d442b0fce"]},{"operation":"removePinnedEntry","args":["lib/armeabi-v7a/libqcOpenSSL.so","adc7ce12e3cb43648984be7b14e75ef9aa0b5a8d988e3b2780e851ef0f046bed"]},{"operation":"removePinnedEntry","args":["lib/armeabi-v7a/libssl_1_1.so","648ce968acce7301aef44105d5a1295c1c33f4aef83c2cede665794b8e0f3eed"]},{"operation":"removePinnedEntry","args":["lib/armeabi-v7a/libtmefriendship.so","b7574f9442ec7d2a525a2707375014bc5ac0acdce5a610746436d0f63be94b91"]}])");
        const auto &stock_statusbar = qinggan11_metadata.at("api30-stock-statusbar.json");

        if (stock_statusbar.at("api") != 30 ||
            stock_statusbar.at("constants").at("original_sha256_allowlist") !=
                    Json::array(
                            {"e66e289a7d5f7bd807a1310b91b83758a2eb8b1400dd882d59ebc57bab49c897"}) ||
            stock_statusbar.at("calls") != Json::array({Json{{"operation", "guardStockStatusBar"},
                                                             {"args", Json::array()}}})) {
            throw std::runtime_error("Free stock statusbar exact recipe contract differs");
        }

        const auto &media = qinggan11_metadata.at("api30-media-runtime.json");

        if (media.at("api") != 30 ||
            media.at("constants").at("original_sha256_allowlist") !=
                    Json::array(
                            {"533a0b0dc5ccbe91a42847fac20c5735bd8d78a28bbb980a6bf365dd5ed0fba0"}) ||
            media.at("calls") != media_calls) {
            throw std::runtime_error(
                    "Free genuine Media exact owner/native/library contract differs");
        }

        const auto &navigation_camera =
                qinggan11_metadata.at("api30-navigation-camera-libraries.json");

        if (navigation_camera.at("api") != 30 ||
            navigation_camera.at("constants").at("original_sha256_allowlist") !=
                    Json::array(
                            {"dceddff70db38bacb50ebffa46d36da76f70e5596c6184bca51c48c3ed562a05",
                             "c680a80c0bfb13911979693c7dc46e8f75df69b361fc6f17a3a94aef28fbaabf"}) ||
            navigation_camera.at("calls") !=
                    Json::array({Json{{"operation", "useLibraries"},
                                      {"args", Json::array({Json::array(
                                                       {"QGAPI", "QGMOD", "QGVehicle"})})}}})) {
            throw std::runtime_error(
                    "Free genuine Navi/Camera two-owner libraries must preserve DEX and native entries");
        }

        const auto &usb_player = qinggan11_metadata.at("api30-usb-player.json");

        if (usb_player.at("api") != 30 ||
            usb_player.at("constants").at("original_sha256_allowlist") !=
                    Json::array(
                            {"533a0b0dc5ccbe91a42847fac20c5735bd8d78a28bbb980a6bf365dd5ed0fba0"}) ||
            usb_player.at("calls") !=
                    Json::array({Json{{"operation", "guardUsbPlayer"}, {"args", Json::array()}}})) {
            throw std::runtime_error(
                    "Free USB original controller/platform decoder recipe differs");
        }

        const auto &kaola = qinggan11_metadata.at("api30-media-cloud-availability.json");

        if (kaola.at("api") != 30 ||
            kaola.at("constants").at("original_sha256_allowlist") !=
                    Json::array(
                            {"533a0b0dc5ccbe91a42847fac20c5735bd8d78a28bbb980a6bf365dd5ed0fba0"}) ||
            kaola.at("calls") != Json::array({Json{{"operation", "guardMediaCloudAvailability"},
                                                   {"args", Json::array()}}})) {
            throw std::runtime_error(
                    "Free CloudFM exact unavailable initializer/service/config contract differs");
        }

        const auto music_pin =
                Json::array({"3dfa09d39638888de1f79d679867d1ac57c9a165a0eed6d43b39aedc4be1ea33"});

        for (const auto &[name, operation] :
             {std::pair{"api30-music-page.json", "guardMusicPage"},
              std::pair{"api30-music-fullscreen.json", "guardMusicFullscreen"}}) {
            if (qinggan11_metadata.at(name).at("api") != 30 ||
                qinggan11_metadata.at(name).at("constants").at("original_sha256_allowlist") !=
                        music_pin ||
                qinggan11_metadata.at(name).at("calls") !=
                        Json::array({Json{{"operation", operation}, {"args", Json::array()}}})) {
                throw std::runtime_error(
                        "Free Music exact page/fullscreen recipe contract differs");
            }
        }

        const auto vehicle_pins =
                Json::array({"b7b80ed1ff6b7b054d6035c6e39e7c04d97e9f2d906cd42f82f9da3a61e2fa2b",
                             "f37a53f1397678af472a87d89d903ba8f6ed6ac892d83ae3117b57bbe3b21360"});

        if (qinggan11_metadata.at("api30-vehicle-libraries.json").at("api") != 30 ||
            qinggan11_metadata.at("api30-vehicle-libraries.json")
                            .at("constants")
                            .at("original_sha256_allowlist") != vehicle_pins ||
            qinggan11_metadata.at("api30-vehicle-libraries.json").at("calls") !=
                    Json::array({Json{{"operation", "useLibraries"},
                                      {"args", Json::array({Json::array(
                                                       {"QGAPI", "QGMOD", "QGVehicle"})})}}})) {
            throw std::runtime_error(
                    "Free Vehicle exact two-owner library recipe contract differs");
        }

        const auto free_profile = read_private_json(
                root / "profiles/free-11-chinese-20260731-7.3.1/profile.json", 512 * 1024);

        if (free_profile.at("compatibility").at("hiboard_top_gesture").at("helper_dex_sha256") !=
            "3b941d112b5a7e676b4d5981d2c3169ad94b284567cd512da13e822a11a91a7c") {
            throw std::runtime_error(
                    "Free OEM top-gesture recipe/profile pin or evidence scope differs");
        }

        if (free_profile.at("patches").contains("original-navi-vehicle") &&
            free_profile.at("patches").at("original-navi-vehicle") !=
                    Json{{"type", "jar"},
                         {"input", "original:QGVehicle"},
                         {"steps", Json::array({"original-navi-vehicle.json"})}}) {
            throw std::runtime_error("Free original Navi vehicle JAR recipe differs");
        }

        {
            std::ifstream input(root /
                                "profiles/free-11-chinese-20260731-7.3.1/files/nvram-active.txt");
            const std::string nvram((std::istreambuf_iterator<char>(input)), {});

            if (nvram.find("\n43=134\n") == std::string::npos) {
                throw std::runtime_error("Free Navi factory differs from profile hardware NVRAM");
            }
        }

        if (free_profile.at("patches").at("MusicConcate") !=
            Json{{"input", "original:MusicConcate"},
                 {"type", "apk"},
                 {"steps",
                  Json::array({"framework-libraries.json", "music-optional-arm32.json",
                               "api30-music-page.json", "api30-music-fullscreen.json"})}}) {
            throw std::runtime_error(
                    "Free Music actual original and ordered page/fullscreen recipes differ");
        }

        for (const auto *name : {"VehicleAir", "VehicleSetting"}) {
            if (free_profile.at("patches").at(name) !=
                Json{{"input", "original:" + std::string(name)},
                     {"type", "apk"},
                     {"steps", Json::array({"api30-vehicle-libraries.json"})}}) {
                throw std::runtime_error("Free Vehicle actual original and library recipe differ");
            }
        }

        const auto hiboard_pin =
                Json::array({"902562587355d9e48d3878773da7955cd5a793c03e5ba6e5b595a317c7ec0f39"});

        if (qinggan11_metadata.at("api30-hiboard-libraries.json").at("calls") !=
                    Json::array({Json{{"operation", "useLibraries"},
                                      {"args", Json::array({Json::array(
                                                       {"QGAPI", "QGMOD", "QGVehicle"})})}}}) ||
            qinggan11_metadata.at("api30-hiboard-routing.json").at("calls") !=
                    Json::array({Json{{"operation", "routeHiboard"}, {"args", Json::array()}}}) ||
            qinggan11_metadata.at("api30-hiboard-libraries.json")
                            .at("constants")
                            .at("original_sha256_allowlist") != hiboard_pin ||
            qinggan11_metadata.at("api30-hiboard-routing.json")
                            .at("constants")
                            .at("original_sha256_allowlist") != hiboard_pin) {
            throw std::runtime_error("Hiboard exact one-pin recipes and property adapter differ");
        }

        for (const auto *name : {"api30-hiboard-libraries.json", "api30-hiboard-routing.json"}) {
            if (qinggan11_metadata.at(name).at("sourceSha256") !=
                sha256_file(root / "profiles/free-11-chinese-20260731-7.3.1/patches" / name)) {
                throw std::runtime_error("Hiboard recipe metadata not bound to actual source");
            }
        }

        const auto metadata = profile_patch_metadata(root, qinggan09_profile());

        if (metadata.size() != 45 || !metadata.contains("systemservice-network.json")) {
            throw std::runtime_error("JSON recipe coverage differs");
        }

        const auto &qinggan09_usb_recipe = metadata.at("api28-usb-player.json");
        const auto qinggan09_usb_definition = read_private_json(
                root / "profiles" / qinggan09_profile() / "profile.json", 512 * 1024);

        if (qinggan09_usb_recipe !=
                    Json{{"schema", 1},
                         {"api", 28},
                         {"constants", Json::object()},
                         {"calls", Json::array()},
                         {"sourceSha256", qinggan09_usb_recipe.at("sourceSha256")}} ||
            qinggan09_usb_recipe.at("sourceSha256") !=
                    sha256_file(root / "profiles" / qinggan09_profile() /
                                "patches/api28-usb-player.json") ||
            qinggan09_usb_definition.at("original").at("QGMedia").at("sha256") !=
                    "4bc4c964d939541f6e11f4634df3d07bca06fb5dae3490e7284edf95d4c15e3f" ||
            qinggan09_usb_definition.at("patches").at("QGMedia").at("input") !=
                    "original:QGMedia" ||
            qinggan09_usb_definition.at("patches").at("QGMedia").at("steps") !=
                    Json::array({"media-runtime.json", "api28-usb-player.json",
                                 "add-qg-runtime.json"})) {
            throw std::runtime_error(
                    "A9 OEM USB decoder must retain exact recipe, original APK, helper and ordered preparation steps");
        }

        for (const auto *service : {"com.qinggan.media.usbmusic.LocalMediaBrowserService",
                                    "com.qinggan.media.usbmusic.LocalMedia1BrowserService",
                                    "com.qinggan.media.usbmusic.LocalMedia2BrowserService"}) {
            if (metadata.at("media-runtime.json")
                        .at("contract")
                        .at("browser_classes")
                        .contains(service)) {
                throw std::runtime_error(
                        "A9 OEM USB browser/queue must not be replaced with a metadata stub");
            }
        }

        const auto &phone_recipe = metadata.at("phone-provider.json");
        const auto phone_definition = read_private_json(
                root / "profiles" / qinggan09_profile() / "profile.json", 512 * 1024);

        if (phone_recipe.at("schema") != 1 || phone_recipe.at("api") != 28 ||
            phone_recipe.at("sourceSha256") != sha256_file(root / "profiles" / qinggan09_profile() /
                                                           "patches/phone-provider.json") ||
            phone_recipe.at("calls") != Json::array() ||
            phone_recipe.at("constants") != Json::object() ||
            phone_definition.at("original").at("BluetoothPhone").at("sha256") !=
                    "d5a7417be79e2e4bfd70e643df85ae76e1a8a15332a345f126ccc5781f5ba8a3" ||
            phone_definition.at("patches").at("BluetoothPhone").at("input") !=
                    "original:BluetoothPhone" ||
            phone_definition.at("patches").at("BluetoothPhone").at("steps") !=
                    Json::array({"systemservice-framework.json", "add-qg-runtime.json",
                                 "phone-provider.json"})) {
            throw std::runtime_error(
                    "A9 own-code phone provider must retain its exact source pin, original APK and ordered preparation steps");
        }

        if (metadata.at("systemui-display-routing.json") !=
            Json{{"api", 28},
                 {"calls", Json::array()},
                 {"constants", Json::object()},
                 {"schema", 1},
                 {"sourceSha256", sha256_file(root / "profiles" / qinggan09_profile() /
                                              "patches/systemui-display-routing.json")}}) {
            throw std::runtime_error(
                    "A9 SystemUI display recipe must retain its exact empty declarative contract");
        }

        for (auto it = metadata.begin(); it != metadata.end(); ++it) {
            if (!it.key().ends_with(".json") ||
                it.value().at("sourceSha256") !=
                        sha256_file(root / "profiles" / qinggan09_profile() / "patches" /
                                    it.key())) {
                throw std::runtime_error("recipe metadata not bound to JSON source");
            }
        }

        std::cout << "native profile artifact extraction coverage and preflight guards passed\n";

        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';

        return 1;
    }
}
