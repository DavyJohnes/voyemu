#include "vehicle_capture.hpp"
#include "process.hpp"
#include "qnx_originals_directory.hpp"
#include <fstream>
#include <iostream>
using namespace voyemu;
namespace {
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

void zip(const std::filesystem::path &file, const Json &originals) {
    std::string local, central;

    for (auto it = originals.begin(); it != originals.end(); ++it) {
        const auto name = "qnx/" + it.key(), bytes = "owner-" + it.key();
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

    const auto offset = local.size();
    local += central;
    word(local, 0x06054b50, 4);
    word(local, 0, 2);
    word(local, 0, 2);
    word(local, originals.size(), 2);
    word(local, originals.size(), 2);
    word(local, central.size(), 4);
    word(local, offset, 4);
    word(local, 0, 2);
    std::ofstream output(file, std::ios::binary);
    output.write(local.data(), local.size());

    if (!output) {
        throw std::runtime_error("fixture ZIP write");
    }
}
} // namespace

int main(int argc, char **argv) {
    try {
        if (argc == 4 && std::string_view(argv[1]) == "--cache") {
            std::cout << materialize_qnx_originals(std::filesystem::canonical(argv[2]), argv[3], {},
                                                   true)
                                 .dump()
                      << '\n';

            return 0;
        }

        bool reserve_bound = false;
        try {
            materialize_qnx_originals({}, "fixture", {}, true, (20ULL << 30) + 1);
        } catch (const std::exception &error) {
            reserve_bound = std::string(error.what()).starts_with("QNX_ORIGINALS_RESERVE_BOUND:");
        }

        if (!reserve_bound) {
            throw std::runtime_error("materialization reserve bound accepted");
        }

        bool failed = false;
        try {
            capture_vehicle_inputs("relative", "fixture", "physical");
        } catch (...) {
            failed = true;
        }

        if (!failed) {
            throw std::runtime_error("relative capture allowed");
        }

        const auto root = std::filesystem::path(__FILE__)
                                  .parent_path()
                                  .parent_path()
                                  .parent_path()
                                  .parent_path() /
                          ".local/build/tmp" / ("vehicle-capture-guards-" + random_token());
        ensure_private_directory(root);
        struct Cleanup {
            std::filesystem::path p;

            ~Cleanup() {
                std::error_code e;
                std::filesystem::remove_all(p, e);
            }
        } cleanup{root};
        const auto old_id = "free-11-chinese-20250710-6.11.1";
        const auto old_dir = root / "profiles" / old_id;
        ensure_private_directory(old_dir);
        Json foreign_originals = Json::object();

        for (const auto *name :
             {"hyp_la.img", "ifs2_la.img", "system_la.img", "ifs2_recovery_la.img"}) {
            foreign_originals[name] = {
                    {"file", name}, {"bytes", 1}, {"sha256", std::string(64, '0')}};
        }

        write_private_json(old_dir / "profile-qnx.json",
                           {{"schema", 1},
                            {"hardware_adapter", "sa8155-qinggan11-731"},
                            {"originals", foreign_originals}});
        bool old_denied = false;
        try {
            materialize_qnx_originals(root, old_id, {}, true);
        } catch (const std::exception &e) {
            old_denied = std::string(e.what()).starts_with("QNX_ORIGINALS_PROFILE:");
        }

        if (!old_denied) {
            throw std::runtime_error("6.11 accepted foreign four-role descriptor");
        }

        for (const auto &serial : {"", "emulator-5554", "device;reboot"}) {
            failed = false;
            try {
                capture_vehicle_inputs(root, "fixture", serial);
            } catch (...) {
                failed = true;
            }

            if (!failed) {
                throw std::runtime_error("implicit/emulated/invalid serial allowed");
            }
        }

        const auto profile = root / "profiles/fixture";
        ensure_private_directory(profile);
        Json originals = Json::object();

        for (const auto *name :
             {"hyp_la.img", "ifs2_la.img", "system_la.img", "ifs2_recovery_la.img"}) {
            originals[name] = {{"file", name}, {"sha256", std::string(64, '0')}};
        }

        write_private_json(profile / "profile-qnx.json", {{"schema", 1}, {"originals", originals}});
        failed = false;
        try {
            materialize_qnx_originals(root, "fixture", {}, true);
        } catch (const std::exception &error) {
            failed = std::string(error.what()).starts_with("QNX_ORIGINALS_MISSING:");
        }

        if (!failed) {
            throw std::runtime_error("missing owner originals/update did not fail before tools");
        }

        if (std::filesystem::exists(root / ".local")) {
            throw std::runtime_error("capture guards installed tools or contacted a device");
        }

        const std::string passion_id = "passion-11-russian-20260403-2.1.4.80";
        const Json passion = Json::parse(
                R"PASSION({"schema":1,"status":"incomplete","hardware_adapter":"sa8155-passion-21480","update":{"source":"owner-update","container":"zip-stored-tgz","sha256":"a1b651223453ed23d1835a70a5cac64c43247a21165ccd88d56a21cc3215ddb9","nested_member":"h53y-cdc-8155-H53_8155_OTA_1.4.0_release-soc_ota-user-secure-2.1.4.80-20260403174306.tgz","nested_sha256":"00d41ae4ff1283040e97276d3b287daf381b2ee9843a7b0f3977ec5b7a06eae4"},"originals":{"mifs_hyp_la.img":{"file":"mifs_hyp_la.img","bytes":2254664,"sha256":"1e7c487aae4f4353621f8d0301b352cc974e9037d114ff3ec2556d048512ad28","acquisition":{"kind":"raw-tar-member","member":"otaPack/mifs_hyp_la.img"}},"mifs_recovery_hyp_la.img":{"file":"mifs_recovery_hyp_la.img","bytes":2330096,"sha256":"8376e6340f719d8a4b6e572fee7a675dd7bd6e88e14c589ba2eba27ee60c239b","acquisition":{"kind":"raw-tar-member","member":"otaPack/mifs_recovery_hyp_la.img"}},"ifs2_la.img":{"file":"ifs2_la.img","bytes":200925932,"sha256":"1e942ab83e59ab78ff0096f474d3677ba7e83f6a00f86a1ebe9f0ae755a3e157","acquisition":{"kind":"raw-tar-member","member":"otaPack/ifs2_la.img"}},"ifs2_recovery_la.img":{"file":"ifs2_recovery_la.img","bytes":32080412,"sha256":"1635c921b73544411a08028005b906e677d029c047d2a6e0a041598c9847709c","acquisition":{"kind":"raw-tar-member","member":"otaPack/ifs2_recovery_la.img"}},"system_la.img":{"file":"system_la.img","bytes":2684354560,"sha256":"9e6395255f9bc51d926fd5ab604612d779de93da59ba71832ee580c8534f42ec","acquisition":{"kind":"raw-tar-member","member":"otaPack/system_la.img"}}}})PASSION");
        const auto passion_directory = root / "profiles" / passion_id;
        ensure_private_directory(passion_directory);
        write_private_json(passion_directory / "profile-qnx.json", passion);
        bool passion_missing = false;
        std::string passion_error;
        try {
            materialize_qnx_originals(root, passion_id, {}, true);
        } catch (const std::exception &error) {
            passion_error = error.what();
            passion_missing = passion_error.starts_with("QNX_ORIGINALS_MISSING:");
        }

        if (!passion_missing) {
            throw std::runtime_error("Passion definition must fail explicitly before tools: " +
                                     passion_error);
        }

        for (unsigned variant = 0; variant < 5; ++variant) {
            auto bad = passion;

            if (variant == 0) {
                bad["status"] = "ready";
            }

            if (variant == 1) {
                bad["hardware_adapter"] = "sa8155-qinggan11-731";
            }

            if (variant == 2) {
                bad["originals"].erase(bad["originals"].begin().key());
            }

            if (variant == 3) {
                bad["originals"].begin().value()["sha256"] = std::string(64, '0');
            }

            if (variant == 4) {
                bad["update"]["sha256"] = std::string(64, '0');
            }

            write_private_json(passion_directory / "profile-qnx.json", bad);
            bool denied = false;
            try {
                materialize_qnx_originals(root, passion_id, {}, true);
            } catch (const std::exception &error) {
                denied = std::string(error.what()).starts_with("QNX_ORIGINALS_PROFILE:");
            }

            if (!denied) {
                throw std::runtime_error("Passion descriptor mismatch escaped exact guard");
            }
        }

        write_private_json(passion_directory / "profile-qnx.json", passion);
        const auto wrong_cache = root / ".local/profiles" / passion_id / "qnx" /
                                 passion.at("originals").begin().key();
        ensure_private_directory(wrong_cache.parent_path());
        {
            std::ofstream out(wrong_cache);
            out << "wrong original";
        }
        const auto wrong_pin = sha256_file(wrong_cache);
        bool preserved = false;
        try {
            materialize_qnx_originals(root, passion_id, {}, true);
        } catch (const std::exception &error) {
            preserved = std::string(error.what()).starts_with("QNX_ORIGINALS_CACHED_SHA:");
        }

        if (!preserved || sha256_file(wrong_cache) != wrong_pin ||
            std::filesystem::exists(root / ".local/toolchains")) {
            throw std::runtime_error(
                    "Passion cached-only failure changed inputs or installed tools");
        }

        const auto inputs = root / ".local/profiles/fixture", qnx = inputs / "qnx";
        ensure_private_directory(qnx);

        for (auto it = originals.begin(); it != originals.end(); ++it) {
            const auto file = qnx / it.key();
            std::ofstream out(file, std::ios::binary);
            out << "owner-" << it.key();
            out.close();
            it.value()["sha256"] = sha256_file(file);
            it.value()["bytes"] = std::uint64_t(std::filesystem::file_size(file));
        }

        write_private_json(profile / "profile-qnx.json",
                           {{"schema", 1},
                            {"originals", originals},
                            {"update", {{"sha256", std::string(64, '0')}}}});
        const auto retained = qnx / "hyp_la.img";
        const auto before = std::filesystem::last_write_time(retained);
        const auto retained_pin = sha256_file(retained);
        const auto cached = materialize_qnx_originals(root, "fixture", {}, true);

        if (cached.at("cached") != true || cached.at("originals_materialized") != 0 ||
            std::filesystem::last_write_time(retained) != before) {
            throw std::runtime_error("verified cache was not reused without archive/tools");
        }

        const auto clone = root / "profiles/local-copy";
        ensure_private_directory(clone);
        write_private_json(
                clone / "profile.json",
                {{"id", "local-copy"},
                 {"capture", {{"profile", "local-copy"}, {"referenceProfile", "fixture"}}}});
        write_private_json(clone / "profile-qnx.json",
                           read_private_json(profile / "profile-qnx.json"));

        if (qnx_originals_directory(root, "local-copy") != qnx ||
            materialize_qnx_originals(root, "local-copy", {}, true).at("cached") != true ||
            std::filesystem::exists(root / ".local/profiles/local-copy")) {
            throw std::runtime_error(
                    "local copy must read reference originals without duplication");
        }

        auto bad_reference = read_private_json(clone / "profile-qnx.json");
        bad_reference["originals"]["hyp_la.img"]["sha256"] = std::string(64, "1"[0]);
        write_private_json(clone / "profile-qnx.json", bad_reference);
        bool mismatch = false;
        try {
            qnx_originals_directory(root, "local-copy");
        } catch (const std::exception &e) {
            mismatch = std::string(e.what()).starts_with("QNX_ORIGINALS_REFERENCE:");
        }

        if (!mismatch) {
            throw std::runtime_error("different reference pins accepted");
        }

        write_private_json(clone / "profile-qnx.json",
                           read_private_json(profile / "profile-qnx.json"));
        ensure_private_directory(root / ".local/profiles/local-copy/qnx");

        if (qnx_originals_directory(root, "local-copy") !=
            root / ".local/profiles/local-copy/qnx") {
            throw std::runtime_error("explicit private inputs ignored");
        }

        std::filesystem::remove(root / ".local/profiles/local-copy/qnx");
        std::filesystem::remove(root / ".local/profiles/local-copy");
        {
            std::ofstream out(retained, std::ios::binary);
            out << "different owner bytes";
        }
        const auto changed = sha256_file(retained);
        failed = false;
        try {
            materialize_qnx_originals(root, "fixture", {}, true);
        } catch (const std::exception &error) {
            failed = std::string(error.what()).starts_with("QNX_ORIGINALS_CACHED_SHA:");
        }

        if (!failed || sha256_file(retained) != changed) {
            throw std::runtime_error("invalid cached original replaced/accepted");
        }

        {
            std::ofstream out(retained, std::ios::binary);
            out << "owner-hyp_la.img";
        }

        if (sha256_file(retained) != retained_pin) {
            throw std::runtime_error("cache fixture restoration differs");
        }

        std::filesystem::remove(qnx / "system_la.img");
        {
            std::ofstream out(inputs / "update.zip", std::ios::binary);
            out << "wrong archive";
        }
        failed = false;
        try {
            materialize_qnx_originals(root, "fixture", {}, true);
        } catch (const std::exception &error) {
            failed = std::string(error.what()).starts_with("QNX_ORIGINALS_UPDATE_SHA:");
        }

        if (!failed || std::filesystem::exists(qnx / "system_la.img") ||
            sha256_file(retained) != retained_pin) {
            throw std::runtime_error("native bad archive touched cache or bypassed exact pin");
        }

        if (std::filesystem::exists(root / ".local/toolchains")) {
            throw std::runtime_error("native cache guards installed Java/SDK");
        }

        const auto toolchain = root / "src/app";
        ensure_private_directory(toolchain);
        const auto fixture_codec = root / "ready-codec.bin";
        {
            std::ofstream out(fixture_codec, std::ios::binary);
            out << "unused codec for raw-only import";
        }
        const Json codec = {{"file", "ready-codec.bin"}, {"sha256", sha256_file(fixture_codec)}};
        write_private_json(
                toolchain / "toolchain.json",
                {{"qnxUpdateCodec", {{"darwin", codec}, {"linux", codec}, {"win32", codec}}}});

        for (auto it = originals.begin(); it != originals.end(); ++it) {
            it.value()["update"] = {{"kind", "raw"}, {"entries", Json::array({"qnx/" + it.key()})}};
        }

        zip(inputs / "update.zip", originals);
        write_private_json(profile / "profile-qnx.json",
                           {{"schema", 1},
                            {"originals", originals},
                            {"update", {{"sha256", sha256_file(inputs / "update.zip")}}}});
        const auto retained_time = std::filesystem::last_write_time(retained);
        const auto imported = materialize_qnx_originals(root, "fixture", {}, true, 0);

        if (imported.at("native") != true || imported.at("originals_materialized") != 1 ||
            std::filesystem::last_write_time(retained) != retained_time ||
            sha256_file(retained) != retained_pin ||
            sha256_file(qnx / "system_la.img") !=
                    originals.at("system_la.img").at("sha256").get<std::string>()) {
            throw std::runtime_error(
                    "native partial import changed cached inputs or did not promote missing image");
        }

        if (std::filesystem::exists(root / ".local/toolchains")) {
            throw std::runtime_error("native raw import installed Java/SDK");
        }

        std::cout
                << "owner capture, verified cache and native partial update import passed; no Java/device/tool run\n";

        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';

        return 1;
    }
}
