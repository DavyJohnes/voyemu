#include "android_adb_route.hpp"
#include "android_embedded_adb.hpp"
#include "android_reboot.hpp"
#include "model.hpp"
#include "process.hpp"
#include "profile_catalog.hpp"

#include <array>
#include <chrono>
#include <fstream>
#include <iostream>
#include <mutex>
#include <regex>
#include <sstream>
#include <thread>

namespace {
using Clock = std::chrono::steady_clock;
using namespace std::chrono_literals;
namespace fs = std::filesystem;

void require(bool condition, const char *message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

voyemu::Json json_file(const fs::path &file) {
    std::ifstream input(file);

    require(bool(input), "input manifest unavailable");

    return voyemu::Json::parse(input);
}

void write_file(const fs::path &file, const std::string &contents) {
    std::ofstream output(file);

    output << contents;
    output.close();
    require(bool(output), "private AVD file unavailable");
}

struct ProofFiles {
    std::array<fs::path, 4> paths;

    ~ProofFiles() {
        for (const auto &path : paths) {
            std::error_code ignored;
            fs::remove_all(path, ignored);
        }
    }
};

constexpr std::uintmax_t transfer_bytes = 100 * 1024 * 1024;

void make_transfer_input(const fs::path &path) {
    constexpr std::size_t block_bytes = 64 * 1024;
    std::array<unsigned char, block_bytes> block{};
    std::uint32_t random = 0x12345678;
    std::ofstream output(path, std::ios::binary);

    for (std::uintmax_t position = 0; position < transfer_bytes; position += block.size()) {
        for (auto &value : block) {
            random ^= random << 13;
            random ^= random >> 17;
            random ^= random << 5;
            value = static_cast<unsigned char>(random);
        }

        output.write(reinterpret_cast<const char *>(block.data()), block.size());
        require(bool(output), "sync fixture write failed");
    }

    output.close();
    require(bool(output) && fs::file_size(path) == transfer_bytes, "sync fixture has wrong size");
}
} // namespace

int main(int argc, char **argv) {
    try {
        require(argc == 2, "usage: android_direct_adb_proof /absolute/voyemu");
        const auto root = fs::canonical(argv[1]);
        const auto sdk = root / ".local/toolchains/native-sdk";
        const auto toolchain = json_file(root / "src/app/toolchain.json").at("sdk");
        require(toolchain.at("emulator") == "36.3.10", "unexpected Emulator version");
        const auto sdk_pins = toolchain.at("runtimeFilePins").at("darwin").at("arm64").at("files");

        for (const char *name :
             {"emulator/emulator", "emulator/qemu/darwin-aarch64/qemu-system-aarch64",
              "emulator/qemu/darwin-aarch64/qemu-system-aarch64-headless"}) {
            require(voyemu::sha256_file(sdk / name) == sdk_pins.at(name).get<std::string>(),
                    "SDK executable pin differs");
        }

        const auto machine = voyemu::embedded_profile_names(voyemu::ProfilePlatform::qinggan09,
                                                            voyemu::ProfileBoard::sa8155)
                                     .at(0);
        const auto firmware = json_file(root / "machines" / machine / "runtime.json").at("android");
        require(firmware.at("api") == 28 && firmware.at("abi") == "arm64-v8a",
                "expected A9 Android base");
        const auto archive = firmware.at("base_archive_sha256").get<std::string>();
        require(std::regex_match(archive, std::regex("[0-9a-f]{64}")), "invalid A9 base identity");
        const auto base = root / ".local/profiles/common/android-base" / archive;
        const auto manifest = json_file(base / "base.json");
        require(manifest.at("archive_sha256").get<std::string>() == archive,
                "base archive pin differs");

        for (const auto &[name, pin] : manifest.at("files").items()) {
            const auto file = base / name;
            require(fs::canonical(file) == file &&
                            fs::file_size(file) == pin.at("bytes").get<std::uintmax_t>() &&
                            voyemu::sha256_file(file) == pin.at("sha256").get<std::string>(),
                    "A9 base input pin differs");
        }

        constexpr unsigned port = 5678;
        const auto nonce = voyemu::random_token();
        const auto binding = voyemu::android_owned_adb(port, port + 30000, nonce);
        const auto isolated = [&] {
            require(voyemu::tcp_port_free(binding.server) && voyemu::tcp_port_free(5037),
                    "unexpected SDK ADB-server listener");
        };
        isolated();
        require(voyemu::tcp_port_free(port) && voyemu::tcp_port_free(port + 1),
                "test emulator ports occupied");
        const auto job = root / ".local/build/jobs" /
                         ("android-direct-adb-proof-" + nonce.substr(0, 12));
        const auto avd = "voyemu-direct-adb-" + nonce.substr(0, 12);
        const auto home = job / "emulator-home";
        const auto avd_home = job / "avd-home";
        const auto data = job / "guest";
        require(!fs::exists(job), "proof directory already exists");
        const ProofFiles cleanup{{home, avd_home, data, job / "tmp"}};

        for (const auto &directory : cleanup.paths) {
            voyemu::ensure_private_directory(directory);
        }

        write_file(avd_home / (avd + ".ini"),
                   "avd.ini.encoding=UTF-8\npath=" + data.string() + "\ntarget=android-28\n");
        write_file(
                data / "config.ini",
                "AvdId=" + avd + "\nimage.sysdir.1=" + base.string() +
                        "/\nabi.type=arm64-v8a\ntag.id=default\nhw.cpu.arch=arm64\nhw.ramSize=1536\nhw.cpu.ncore=2\n"
                        "hw.gpu.enabled=yes\nhw.gpu.mode=host\nhw.lcd.width=720\nhw.lcd.height=480\nhw.lcd.density=160\n"
                        "disk.dataPartition.size=2G\nfastboot.forceColdBoot=yes\nfastboot.forceFastBoot=no\nhw.sdCard=no\n");
        std::cout << "proof_job=" << job.string() << std::endl;

        voyemu::ChildProcess emulator;
        std::mutex emulator_mutex;
        voyemu::AndroidEmbeddedAdb adb(port);

        voyemu::ProcessSpec spec;
        spec.executable = sdk / "emulator/emulator";
        spec.directory = job;
        spec.output = job / "emulator.log";
        spec.environment = voyemu::android_owned_adb_sdk_environment(binding);
        voyemu::android_session_environment(spec, job);
        spec.environment["ANDROID_HOME"] = sdk.string();
        spec.environment["ANDROID_SDK_ROOT"] = sdk.string();
        spec.environment["ANDROID_AVD_HOME"] = avd_home.string();
        spec.environment["ADB_VENDOR_KEYS"] = "";
        spec.arguments = {"-avd",
                          avd,
                          "-port",
                          std::to_string(port),
                          "-sysdir",
                          base.string(),
                          "-datadir",
                          data.string(),
                          "-data",
                          (data / "userdata-qemu.img").string(),
                          "-initdata",
                          (base / "userdata.img").string(),
                          "-cache",
                          (data / "cache.img").string(),
                          "-no-snapshot",
                          "-read-only",
                          "-no-window",
                          "-no-audio",
                          "-no-boot-anim",
                          "-accel",
                          "on",
                          "-prop",
                          "qemu.voyemu.token=" + nonce};
        emulator = voyemu::ChildProcess::launch(spec);
        const auto emulator_pid = emulator.pid();
        std::cout << "proof_emulator_pid=" << emulator_pid << std::endl;
        const auto owns = [&] {
            std::lock_guard lock(emulator_mutex);

            return emulator.owns_tcp_listener(port) && emulator.owns_tcp_listener(port + 1);
        };
        const auto deadline = Clock::now() + 180s;

        while (Clock::now() < deadline && !owns()) {
            require(emulator.alive(), "test emulator exited before opening listeners");
            isolated();
            std::this_thread::sleep_for(100ms);
        }

        require(owns(), "owned test emulator listener timeout");
        isolated();
        adb.attach(owns);

        const auto command = [&](const std::vector<std::string> &arguments, int timeout_ms) {
            require(owns(), "test emulator ownership changed before ADB");
            isolated();
            const auto lease = adb.lease();
            const auto result = adb.run(arguments, timeout_ms, 4096, {}, lease);
            require(owns(), "test emulator ownership changed during ADB");
            isolated();

            return result;
        };
        const auto ready = command({"wait-for-device"}, 10000);
        require(!ready.timed_out && ready.exit_code == 0, "owned direct ADB transport unavailable");

        std::string boot_id, secure;

        while (Clock::now() < deadline && boot_id.empty()) {
            const auto result = command(
                    {"shell", "getprop qemu.voyemu.token; cat /proc/sys/kernel/random/boot_id; "
                              "getprop sys.boot_completed; getprop ro.adb.secure"},
                    2000);

            if (!result.timed_out && result.exit_code == 0) {
                std::string token, boot, completed;
                std::istringstream output(result.output);
                std::getline(output, token);
                std::getline(output, boot);
                std::getline(output, completed);
                std::getline(output, secure);
                require(token == nonce, "test emulator guest nonce differs");

                if (completed == "1" && voyemu::android_boot_id_valid(boot)) {
                    boot_id = boot;
                }
            }

            if (boot_id.empty()) {
                std::this_thread::sleep_for(250ms);
            }
        }

        require(!boot_id.empty(), "test guest boot did not complete");
        const auto slow = command({"shell", "sleep 3"}, 100);
        require(slow.timed_out, "slow direct operation did not time out");
        const auto confirm_guest = [&] {
            const auto identity = command(
                    {"shell", "getprop qemu.voyemu.token; cat /proc/sys/kernel/random/boot_id"},
                    2000);
            require(!identity.timed_out && identity.exit_code == 0 &&
                            identity.output == nonce + "\n" + boot_id + "\n",
                    "direct ADB did not retain the same guest boot");
        };
        confirm_guest();

        const auto source = job / "tmp/sync-source.bin";
        const auto destination = job / "tmp/sync-destination.bin";
        const auto guest_folder = "/data/local/tmp/voyemu-sync-" + nonce;
        const auto guest_file = guest_folder + "/transfer.bin";
        make_transfer_input(source);
        const auto digest = voyemu::sha256_file(source);
        const auto mkdir = command({"shell", "mkdir", "-m", "700", guest_folder}, 2000);
        require(!mkdir.timed_out && mkdir.exit_code == 0, "private guest sync folder unavailable");
        const auto pushed = command({"push", source.string(), guest_file}, 90000);
        require(!pushed.timed_out && pushed.exit_code == 0, "100 MiB direct push failed");
        const auto guest_digest = command({"shell", "sha256sum", guest_file}, 15000);
        require(!guest_digest.timed_out && guest_digest.exit_code == 0 &&
                        guest_digest.output.starts_with(digest + " "),
                "guest sync digest differs");
        confirm_guest();

        const auto pulled = command({"pull", guest_file, destination.string()}, 90000);

        if (pulled.timed_out || pulled.exit_code != 0) {
            throw std::runtime_error("100 MiB direct pull failed: " +
                                     (pulled.output.starts_with("ANDROID_ADB_SERVICE ")
                                              ? pulled.output
                                      : pulled.timed_out ? std::string("deadline elapsed")
                                                         : std::string("guest rejected transfer")));
        }

        require(fs::file_size(destination) == transfer_bytes, "100 MiB direct pull size differs");
        require(voyemu::sha256_file(destination) == digest, "100 MiB direct pull digest differs");
        const auto missing = command({"pull", guest_folder + "/missing.bin", destination.string()},
                                     5000);
        require(!missing.timed_out && missing.exit_code != 0 &&
                        missing.output.find("No such file or directory") != std::string::npos,
                "missing guest file did not return a sync failure");
        require(fs::file_size(destination) == transfer_bytes &&
                        voyemu::sha256_file(destination) == digest,
                "failed pull replaced or truncated the existing destination");

        for (const auto &entry : fs::directory_iterator(destination.parent_path())) {
            require(!entry.path().filename().string().starts_with(".voyemu-adb-"),
                    "failed pull left a partial host file");
        }

        confirm_guest();
        const auto removed = command({"shell", "rm", "-r", guest_folder}, 2000);
        require(!removed.timed_out && removed.exit_code == 0, "guest sync fixture cleanup failed");

        const auto owns_console = [&] {
            std::lock_guard lock(emulator_mutex);

            return emulator.pid() == emulator_pid && emulator.owns_tcp_listener(port);
        };
        const auto before_reboot = adb.lease();
        require(owns() && owns_console(), "test emulator ownership changed before reboot");
        isolated();
        const auto reboot = adb.run({"reboot"}, 15000, 4096, {}, before_reboot);
        require(!reboot.timed_out && reboot.exit_code == 0 && owns_console(),
                "owned guest reboot submission failed");
        const auto reboot_deadline = Clock::now() + 120s;
        std::string rebooted_boot;

        while (Clock::now() < reboot_deadline && rebooted_boot.empty()) {
            require(owns_console(), "owned console lost during guest reboot");
            isolated();
            const auto reconnect = adb.run({"wait-for-device"}, 1000, 4096, {}, before_reboot);

            if (!reconnect.timed_out && reconnect.exit_code == 0 && owns()) {
                const auto identity = adb.run(
                        {"shell",
                         "getprop qemu.voyemu.token; cat /proc/sys/kernel/random/boot_id; getprop sys.boot_completed"},
                        2000, 4096, {}, adb.lease());

                if (!identity.timed_out && identity.exit_code == 0) {
                    std::string token, boot, completed;
                    std::istringstream output(identity.output);
                    std::getline(output, token);
                    std::getline(output, boot);
                    std::getline(output, completed);
                    require(token == nonce, "reconnected guest nonce differs");

                    if (completed == "1" && boot != boot_id &&
                        voyemu::android_boot_id_valid(boot)) {
                        rebooted_boot = boot;
                    }
                }
            }

            if (rebooted_boot.empty()) {
                std::this_thread::sleep_for(100ms);
            }
        }

        require(!rebooted_boot.empty() && owns() && owns_console(),
                "owned guest did not reconnect on a new Linux boot");
        isolated();

        require(adb.shutdown(), "direct host shutdown failed");
        emulator.terminate(3000);
        isolated();
        require(!emulator.alive() && voyemu::tcp_port_free(port) && voyemu::tcp_port_free(port + 1),
                "owned test emulator cleanup failed");

        for (const auto &directory : cleanup.paths) {
            fs::remove_all(directory);
        }

        voyemu::write_private_json(job / "proof.json",
                                   {{"base_archive_sha256", archive},
                                    {"emulator", "36.3.10"},
                                    {"emulator_pid", emulator_pid},
                                    {"console_port", port},
                                    {"adbd_port", port + 1},
                                    {"private_adb_server_port", binding.server},
                                    {"private_adb_server_absent", true},
                                    {"guest_nonce_verified", true},
                                    {"boot_verified", true},
                                    {"timeout_recovery_verified", true},
                                    {"guest_ro_adb_secure", secure},
                                    {"cleanup_verified", true},
                                    {"default_adb_server_absent", true},
                                    {"sync_transfer_bytes", transfer_bytes},
                                    {"sync_sha256", digest},
                                    {"sync_guest_digest_verified", true},
                                    {"sync_round_trip_verified", true},
                                    {"sync_failed_pull_preserved_existing_file", true},
                                    {"warm_reboot_verified", true},
                                    {"reboot_old_boot_id", boot_id},
                                    {"reboot_new_boot_id", rebooted_boot},
                                    {"reboot_same_sdk_pid", true}});
        std::cout << "direct_adb=pass guest_boot=verified timeout_recovery=verified "
                  << "sync_bytes=" << transfer_bytes << " sync_sha256=" << digest
                  << " warm_reboot=verified ro.adb.secure=" << secure << '\n';

        return 0;
    } catch (const std::exception &error) {
        std::cerr << "direct ADB proof: " << error.what() << '\n';

        return 1;
    }
}
