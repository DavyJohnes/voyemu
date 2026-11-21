#include "quickboot.hpp"
#include "android_audio_startup.hpp"
#include "android_engine.hpp"
#include "android_system_binding.hpp"
#include "passion_wheel.hpp"
#include "process.hpp"
#include "qinggan11_startup.hpp"
#include "qnx_interguest_contract.hpp"
#include <cassert>
#include <exception>
#include <fstream>
#include <iostream>
using namespace voyemu;

int main() try {
    const std::string session(32, 'a'), guest = "01234567-89ab-cdef-0123-456789abcdef";
    int saves = 0, requests = 0;
    auto reply = [&](const std::string &nonce) {
        ++requests;

        return Json{{"schema", 1},           {"status", "presave-ready"},
                    {"session_id", session}, {"boot_id", guest},
                    {"sequence", 7},         {"save_nonce", nonce},
                    {"acoustic_zero", true}};
    };
    auto save = [&] {
        ++saves;

        return true;
    };
    auto saved = android_presave_snapshot(session, guest, 7, reply, [&] {
        assert(requests == 1);
        ++saves;

        return true;
    });

    assert(saved && requests == 1 && saves == 1);

    for (const char *field :
         {"schema", "status", "session_id", "boot_id", "sequence", "save_nonce", "acoustic_zero"}) {
        auto bad = [&](const std::string &nonce) {
            auto value = reply(nonce);
            value.erase(field);

            return value;
        };
        assert(!android_presave_snapshot(session, guest, 7, bad, save));
        assert(saves == 1);
    }

    const Json invalid_receipts = {{"schema", 1.0},
                                   {"status", "applied"},
                                   {"session_id", std::string(32, 'b')},
                                   {"boot_id", "11111111-1111-1111-1111-111111111111"},
                                   {"sequence", 7.0},
                                   {"save_nonce", std::string(32, 'c')},
                                   {"acoustic_zero", false}};

    for (const auto &[field, value] : invalid_receipts.items()) {
        auto bad = [&](const std::string &nonce) {
            auto result = reply(nonce);
            result[field] = value;

            return result;
        };
        assert(!android_presave_snapshot(session, guest, 7, bad, save));
        assert(saves == 1);
    }

    auto stale = [&](const std::string &nonce) {
        auto value = reply(nonce);
        value["sequence"] = 6;

        return value;
    };
    const auto stale_saved = android_presave_snapshot(session, guest, 7, stale, save);
    assert(!stale_saved);
    const auto timeout_saved = android_presave_snapshot(
            session, guest, 7,
            [](const std::string &) -> Json {
                throw std::runtime_error("timeout");
            },
            save);
    assert(!timeout_saved);
    assert(saves == 1);
    const auto failed_save = android_presave_snapshot(session, guest, 7, reply, [] {
        throw std::runtime_error("save failure");

        return true;
    });
    assert(!failed_save);

    auto temporary = std::filesystem::current_path() / ".local/build";
    ensure_private_directory(temporary);
    auto root = temporary / ("voyemu-quickboot-test-" + random_token());
    ensure_private_directory(root);
    auto machine = root / "machine";
    ensure_private_directory(machine);

    for (const char *name :
         {"userdata-qemu.img", "cache.img", "encryptionkey.img", "userdata-qemu.img.qcow2",
          "cache.img.qcow2", "encryptionkey.img.qcow2", "system.img.qcow2", "vendor.img.qcow2"}) {
        std::ofstream(machine / name) << "prepared disk";
    }

    ensure_private_directory(machine / "base");

    for (const char *name : {"system.img", "vendor.img"}) {
        std::ofstream(machine / "base" / name) << "prepared base";
    }

    const auto config_template = root / "config-template.ini";
    { std::ofstream(config_template) << "hw.ramSize=4096\n"; }
    const Json spec{{"machine", machine.string()},
                    {"android_sdk_root", (root / "sdk").string()},
                    {"avd_name", "VoyemuFixture"},
                    {"config_template", config_template.string()},
                    {"pins", Json::array({{{"path", "sdk"}, {"sha256", "pinned"}},
                                          {{"path", config_template.string()},
                                           {"sha256", sha256_file(config_template)}}})}};
    const auto original_system = android_system_binding(spec);
    assert(original_system.raw == machine / "base/system.img" &&
           original_system.overlay == machine / "system.img.qcow2");
    auto upgraded = spec;
    const std::string system_sha(64, 'a');
    const auto system_directory = machine / "system-versions" / system_sha;
    upgraded["api"] = 30;
    upgraded["system_binding"] = {{"raw", (system_directory / "system.img").string()},
                                  {"overlay", (system_directory / "system.img.qcow2").string()},
                                  {"sha256", system_sha}};
    const auto selected_system = android_system_binding(upgraded);
    assert(selected_system.raw == system_directory / "system.img" &&
           selected_system.overlay == system_directory / "system.img.qcow2");
    const auto rejected_binding = [](const Json &value) {
        bool rejected = false;
        try {
            (void)android_system_binding(value);
        } catch (...) {
            rejected = true;
        }

        assert(rejected);
    };
    auto invalid_system = upgraded;
    invalid_system["api"] = 28;
    rejected_binding(invalid_system);
    invalid_system = upgraded;
    invalid_system["system_binding"]["extra"] = true;
    rejected_binding(invalid_system);
    invalid_system = upgraded;
    invalid_system["system_binding"]["sha256"] = std::string(64, 'A');
    rejected_binding(invalid_system);
    invalid_system = upgraded;
    invalid_system["system_binding"]["raw"] = (system_directory / "../system.img").string();
    rejected_binding(invalid_system);
    invalid_system = upgraded;
    invalid_system["system_binding"]["raw"] = (machine / "system.img").string();
    rejected_binding(invalid_system);
    invalid_system = upgraded;
    invalid_system["system_binding"]["overlay"] = (machine / "system.img.qcow2").string();
    rejected_binding(invalid_system);
    auto gui_a = spec, gui_b = spec;
    gui_a["frontend"] = {{"executable", "old-window"}, {"capture", true}};
    gui_b["frontend"] = {{"executable", "new-window"}, {"capture", false}};
    assert(android_quickboot_key(gui_a) == android_quickboot_key(gui_b));
    auto network_spec = spec;
    network_spec["interguest_network"] = qnx_interguest_contract();
    const auto network_key = android_quickboot_key(network_spec);
    assert(network_key != android_quickboot_key(spec) &&
           network_key.at("network_topology") == qnx_interguest_contract() &&
           network_key.at("interguest_nic") == "virtio-net-pci-v1");
    auto invalid_network = network_spec;
    invalid_network["interguest_network"]["port"] = 12345;
    bool rejected_network = false;
    try {
        (void)android_quickboot_key(invalid_network);
    } catch (const std::exception &e) {
        rejected_network = std::string(e.what()).starts_with("INTERGUEST_CONTRACT:");
    }

    assert(rejected_network);
    invalid_network = network_spec;
    invalid_network["interguest_network"]["address"] = "192.168.1.1";
    rejected_network = false;
    try {
        (void)android_quickboot_key(invalid_network);
    } catch (const std::exception &e) {
        rejected_network = std::string(e.what()).starts_with("INTERGUEST_CONTRACT:");
    }

    assert(rejected_network);
    auto outbound_spec = spec;
    outbound_spec["network"] = android_network_contract();
    const auto outbound_key = android_quickboot_key(outbound_spec);
    assert(outbound_key != android_quickboot_key(spec) &&
           outbound_key.at("outbound_network_topology") == android_network_contract() &&
           outbound_key.at("outbound_nic") == "virtio-net-pci-nat-v1");
    auto invalid_outbound = outbound_spec;
    invalid_outbound["network"]["address"] = "10.0.2.15";
    bool rejected_outbound = false;
    try {
        (void)android_quickboot_key(invalid_outbound);
    } catch (const std::exception &e) {
        rejected_outbound = std::string(e.what()).starts_with("ANDROID_NETWORK_CONTRACT:");
    }

    assert(rejected_outbound);
    auto audio_spec = spec;
    audio_spec["audio_startup"] = {{"schema", 1}, {"authority", "com.voyemu.output.audio.startup"}};
    assert(android_quickboot_key(audio_spec).at("audio_startup_protocol") == 1);
    assert(android_quickboot_key(audio_spec) != android_quickboot_key(spec));
    Json audio_reply{{"schema", 1},
                     {"launch_session", "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"},
                     {"boot_id", "boot"},
                     {"android_generation", std::uint64_t{1}},
                     {"completed", true},
                     {"all_zero", true},
                     {"oem25_zero", true},
                     {"stream_readbacks", Json::array({0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0})}};
    auto audio_accept = [&](const Json &value, bool zero) {
        validate_android_audio_startup_reply(value, "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa", "boot", 1,
                                             zero);
    };
    audio_accept(audio_reply, true);

    for (const char *field : {"launch_session", "boot_id", "android_generation", "completed",
                              "stream_readbacks", "oem25_zero"}) {
        auto bad = audio_reply;

        if (std::string(field) == "launch_session" || std::string(field) == "boot_id") {
            bad[field] = "wrong";
        } else if (std::string(field) == "android_generation") {
            bad[field] = std::uint64_t{2};
        } else if (std::string(field) == "stream_readbacks") {
            bad[field] = Json::array({0});
        } else {
            bad[field] = false;
        }

        bool rejected = false;
        try {
            audio_accept(bad, true);
        } catch (const std::exception &) {
            rejected = true;
        }

        assert(rejected);
    }

    auto changed_volume = audio_reply;
    changed_volume["stream_readbacks"][3] = 5;
    changed_volume["all_zero"] = false;
    audio_accept(changed_volume, false);
    bool nonzero_rejected = false;
    try {
        audio_accept(changed_volume, true);
    } catch (const std::exception &) {
        nonzero_rejected = true;
    }

    assert(nonzero_rejected);
    // SDK cold reboot reuses retained specification with the old owned ADB lifetime.
    auto retained_reboot_spec = spec;
    retained_reboot_spec["owned_adb"] = {{"schema", 1},
                                         {"owner", std::string(32, 'a')},
                                         {"server_pid", 1234},
                                         {"server_port", 25574},
                                         {"grpc_port", 35574}};
    assert(android_quickboot_key(retained_reboot_spec) == android_quickboot_key(spec));
    auto another_owner = retained_reboot_spec;
    another_owner["owned_adb"]["owner"] = std::string(32, 'b');
    another_owner["owned_adb"]["server_pid"] = 5678;
    assert(android_quickboot_key(another_owner) == android_quickboot_key(spec));
    auto key = android_quickboot_key(spec);
    AndroidQuickBoot cache(machine, android_quickboot_key(retained_reboot_spec));
    assert(!cache.inspect().resume);
    cache.begin();
    assert(!cache.inspect().resume);
    auto directory = machine / "snapshots" / AndroidQuickBoot::snapshot_name;

    for (const char *name : {"snapshot.pb", "ram.bin", "hardware.ini"}) {
        std::ofstream(directory / name) << "completed SDK snapshot";
    }

    constexpr std::uint64_t gib = 1024ULL * 1024 * 1024, reserve = 512ULL * 1024 * 1024;
    assert(cache.snapshot_save_budget(4 * gib).at("allowed") == false);
    auto exact_budget = cache.snapshot_save_budget(4 * gib + reserve);
    assert(exact_budget.at("allowed") == true &&
           exact_budget.at("required_bytes") == 4 * gib + reserve);
    assert(cache.snapshot_save_budget(4 * gib + reserve - 1).at("allowed") == false);
    assert(cache.snapshot_save_budget(std::nullopt).at("allowed") == false);
    const auto ram_size = std::filesystem::file_size(directory / "ram.bin");
#ifndef _WIN32
    std::filesystem::resize_file(directory / "ram.bin",
                                 5 * gib); // Sparse length, not a multi-gigabyte allocation.
    const auto previous_budget = cache.snapshot_save_budget(6 * gib);
    assert(previous_budget.at("allowed") == true &&
           previous_budget.at("required_bytes") == 5 * gib + 2 * ram_size + reserve);
    assert(cache.snapshot_save_budget(5 * gib + reserve).at("allowed") == false);
    std::filesystem::resize_file(directory / "ram.bin", ram_size);
#endif
    const auto before_budget = sha256_file(directory / "ram.bin");
    { std::ofstream(config_template) << "hw.ramSize = 8192\n"; }
    auto larger_spec = spec;
    larger_spec["pins"][1]["sha256"] = sha256_file(config_template);
    const auto larger_budget = AndroidQuickBoot(machine, android_quickboot_key(larger_spec))
                                       .snapshot_save_budget(8 * gib + reserve);
    assert(larger_budget.at("allowed") == true &&
           larger_budget.at("required_bytes") == 8 * gib + reserve);
    { std::ofstream(config_template) << "hw.ramSize=4096\n"; }
    auto invalid_spec = spec;
    invalid_spec["pins"][1]["sha256"] = std::string(64, '0');
    assert(AndroidQuickBoot(machine, android_quickboot_key(invalid_spec))
                   .snapshot_save_budget(8 * gib)
                   .at("allowed") == false);

    for (const char *invalid :
         {"hw.ramSize=0\n", "hw.ramSize=-1\n", "hw.ramSize=4096x\n", "hw.ramSize=65537\n",
          "hw.ramSize=4096\nhw.ramSize=4096\n", "other=4096\n"}) {
        {
            std::ofstream(config_template) << invalid;
        }
        auto changed_spec = spec;
        changed_spec["pins"][1]["sha256"] = sha256_file(config_template);
        assert(AndroidQuickBoot(machine, android_quickboot_key(changed_spec))
                       .snapshot_save_budget(8 * gib)
                       .at("allowed") == false);
    }

    { std::ofstream(config_template) << "hw.ramSize=4096\n"; }
    assert(sha256_file(directory / "ram.bin") == before_budget &&
           cache.snapshot_save_budget(8 * gib).at("allowed") == true);
    auto boot = random_token();
    cache.commit(boot);
    assert(cache.inspect().resume && cache.inspect().boot_nonce == boot);
    auto direct_launch = spec;
    direct_launch["adb_transport"] = "direct";
    AndroidQuickBoot direct_cache(machine, android_quickboot_key(direct_launch));
    assert(direct_cache.inspect().resume && direct_cache.runtime_checkpoint().boot_nonce == boot);
    const auto fresh_sid_checkpoint =
            AndroidQuickBoot(machine, android_quickboot_key(spec)).runtime_checkpoint();
    assert(fresh_sid_checkpoint.resume && fresh_sid_checkpoint.boot_nonce == boot);

    for (const char *material :
         {"port", "sdk_control", "args", "audioOutput", "pins", "features"}) {
        auto changed = spec;

        if (std::string(material) == "port") {
            changed[material] = 5578;
        } else if (std::string(material) == "sdk_control") {
            changed[material] = {{"grpc_port", 35578}};
        } else if (std::string(material) == "args") {
            changed[material] = Json::array({"-accel", "off"});
        } else if (std::string(material) == "audioOutput") {
            changed[material] = true;
        } else if (std::string(material) == "pins") {
            changed[material][0]["sha256"] = std::string(64, 'f');
        } else {
            changed[material] = {{"VirtioInput", "off"}};
        }

        const auto rejected =
                AndroidQuickBoot(machine, android_quickboot_key(changed)).runtime_checkpoint();
        assert(!rejected.resume && rejected.reason == "prepared_inputs_changed");
    }

    const auto legacy_audio =
            AndroidQuickBoot(machine, android_quickboot_key(audio_spec)).runtime_checkpoint();
    assert(!legacy_audio.resume && legacy_audio.reason == "prepared_inputs_changed");

    for (const char *field : {"schema", "launch_session", "boot_id", "android_generation",
                              "completed", "all_zero", "oem25_zero", "stream_readbacks"}) {
        auto missing = audio_reply;
        missing.erase(field);
        bool rejected = false;
        try {
            audio_accept(missing, true);
        } catch (const std::runtime_error &e) {
            rejected = std::string(e.what()).starts_with("ANDROID_AUDIO_STARTUP:");
        }

        assert(rejected);
    }

    assert(cache.runtime_checkpoint().resume && cache.runtime_checkpoint().boot_nonce == boot);
    const auto unavailable = machine / "snapshot-not-enumerated";
    std::filesystem::rename(directory, unavailable);
    assert(!cache.inspect().resume &&
           cache.runtime_checkpoint()
                   .resume); // Runtime avoids the snapshot walk; explicit inspect audits it.
    std::filesystem::rename(unavailable, directory);
#ifdef _WIN32
    {
        assert(key.at("backend_arguments") == android_sdk_machine_arguments(true));
        auto renderer_spec = spec;
        renderer_spec["rendererPolicy"] = "windows-angle-36.3.10";
        const auto renderer_key = android_quickboot_key(renderer_spec);
        assert(renderer_key != key);
        auto changed_renderer = renderer_spec;
        changed_renderer["rendererPolicy"] = "tampered";
        assert(android_quickboot_key(changed_renderer) != renderer_key);
        changed_renderer.erase("rendererPolicy");
        assert(android_quickboot_key(changed_renderer) != renderer_key);
        auto legacy = read_private_json(machine / "voyemu-quickboot.json");
        auto saved = legacy;
        legacy["key"].erase("backend_arguments");
        write_private_json(machine / "voyemu-quickboot.json", legacy);
        assert(!cache.inspect().resume && cache.inspect().reason == "prepared_inputs_changed");
        legacy = saved;
        legacy["key"]["backend_arguments"] = Json::array({"-qemu", "-machine", "type=virt"});
        write_private_json(machine / "voyemu-quickboot.json", legacy);
        assert(!cache.inspect().resume && cache.inspect().reason == "prepared_inputs_changed");
        legacy = saved;
        legacy["key"]["backend_arguments"] =
                Json::array({"-qemu", "-machine", "type=virt", "-accel", "tcg,thread=single"});
        write_private_json(machine / "voyemu-quickboot.json", legacy);
        assert(!cache.inspect().resume && cache.inspect().reason == "prepared_inputs_changed");
        write_private_json(machine / "voyemu-quickboot.json", saved);
        assert(cache.inspect().resume);
    }
#else
    assert(!key.contains("backend_arguments"));
#endif
#ifdef __APPLE__
    const auto identity = read_private_json(machine / "voyemu-quickboot.json");
    const auto volume =
            identity.at("disks").at("userdata-qemu.img").at("volume_uuid").get<std::string>();
    assert(volume.size() == 32 &&
           volume.find_first_not_of("0123456789abcdef") == std::string::npos &&
           volume != std::string(32, '0'));

    for (const auto &group : {"disks", "snapshot"}) {
        for (const auto &item : identity.at(group).items()) {
            assert(item.value().at("volume_uuid") == volume && !item.value().contains("device"));
        }
    }

    auto other_volume = identity;
    other_volume["disks"]["userdata-qemu.img"]["volume_uuid"] = std::string(32, '0');
    write_private_json(machine / "voyemu-quickboot.json", other_volume);
    assert(cache.inspect().reason == "guest_storage_changed");
    write_private_json(machine / "voyemu-quickboot.json", identity);
    assert(cache.inspect().resume);
    auto old_volume = identity;
    old_volume["disks"]["userdata-qemu.img"].erase("volume_uuid");
    old_volume["disks"]["userdata-qemu.img"]["device"] = 123;
    write_private_json(machine / "voyemu-quickboot.json", old_volume);
    assert(cache.inspect().reason == "storage_identity_format_changed");
    write_private_json(machine / "voyemu-quickboot.json", identity);
    assert(cache.inspect().resume);
#endif
    auto legacy = read_private_json(machine / "voyemu-quickboot.json");
    legacy["key"]["prepared"]["frontend"] = {{"executable", "older-private-viewer"}};
    write_private_json(machine / "voyemu-quickboot.json", legacy);
    assert(cache.inspect().resume); // GUI changes do not alter guest hardware or relax
                                    // disk/snapshot identity.
    auto changed = key;
    changed["protocol"] = 3;
    assert(!AndroidQuickBoot(machine, changed).inspect().resume);
    const auto userdata = machine / "userdata-qemu.img";
    const auto userdata_mtime = std::filesystem::last_write_time(userdata);
    {
        std::fstream file(userdata, std::ios::in | std::ios::out);
        file << "new user data";
    }
    std::filesystem::last_write_time(userdata, userdata_mtime);
    assert(cache.inspect().reason == "guest_storage_changed");
    assert(!cache.runtime_checkpoint().resume &&
           cache.runtime_checkpoint().reason == "guest_storage_changed");
    cache.begin();
    cache.commit(boot);
    assert(cache.inspect().resume && cache.runtime_checkpoint().resume);
    auto storage = machine / "cache.img";
    auto old_mtime = std::filesystem::last_write_time(storage);
    {
        std::fstream file(storage, std::ios::in | std::ios::out);
        file << "edited";
    }
    std::filesystem::last_write_time(storage, old_mtime);
    assert(cache.inspect().reason == "guest_storage_changed");
    cache.begin();
    cache.commit(boot);
    { std::ofstream(directory / "ram.bin", std::ios::app) << "incomplete"; }
    assert(cache.inspect().reason == "snapshot_changed");
    cache.begin();
    cache.commit(boot);
    cache.begin();
    assert(!cache.inspect().resume); // crash cannot reuse checkpoint
    AndroidQuickBoot competing(machine, key);
    competing.begin();
    bool generation_rejected = false;
    try {
        cache.commit(boot);
    } catch (...) {
        generation_rejected = true;
    }

    assert(generation_rejected);
    cache.begin();
    cache.commit(boot);
    { std::ofstream(directory / "unexpected.bin") << "changed snapshot"; }
    assert(cache.inspect().reason == "snapshot_changed");
    cache.begin();
    cache.commit(boot);
    { std::ofstream(machine / "base" / "system.img", std::ios::app) << "edited prepared system"; }
    assert(cache.inspect().reason == "guest_storage_changed");
    ensure_private_directory(system_directory);
    {
        std::ofstream(selected_system.raw) << "upgraded system";
        std::ofstream(selected_system.overlay) << "fresh system overlay";
    }
    cache.begin();
    cache.commit(boot);
    AndroidQuickBoot upgraded_cache(machine, android_quickboot_key(upgraded));
    assert(!upgraded_cache.runtime_checkpoint().resume &&
           upgraded_cache.runtime_checkpoint().reason == "prepared_inputs_changed");
    upgraded_cache.begin();
    upgraded_cache.commit(boot);
    assert(upgraded_cache.inspect().resume);
    const auto upgraded_identity = read_private_json(machine / "voyemu-quickboot.json");
    assert(upgraded_identity.at("disks").at("base/system.img") ==
                   cache_file_identity(selected_system.raw) &&
           upgraded_identity.at("disks").at("system.img.qcow2") ==
                   cache_file_identity(selected_system.overlay));
    {
        std::ofstream(machine / "base/system.img", std::ios::app) << "old unselected raw";
        std::ofstream(machine / "system.img.qcow2", std::ios::app) << "old unselected overlay";
    }
    assert(upgraded_cache.inspect().resume);
    { std::ofstream(selected_system.overlay, std::ios::app) << "selected overlay changed"; }
    assert(upgraded_cache.inspect().reason == "guest_storage_changed");
    upgraded_cache.begin();
    upgraded_cache.commit(boot);
    { std::ofstream(selected_system.raw, std::ios::app) << "selected raw changed"; }
    assert(upgraded_cache.inspect().reason == "guest_storage_changed");
    { std::ofstream(machine / "voyemu-quickboot.json.pending") << "interrupted metadata"; }
    cache.begin();
    assert(!std::filesystem::exists(machine / "voyemu-quickboot.json.pending"));
#ifndef _WIN32
    std::filesystem::remove(machine / "voyemu-quickboot.json");
    std::filesystem::create_symlink(directory / "snapshot.pb", machine / "voyemu-quickboot.json");
    assert(!cache.inspect().resume);
    bool rejected = false;
    try {
        cache.begin();
    } catch (...) {
        rejected = true;
    }

    assert(rejected);
    std::filesystem::remove(machine / "voyemu-quickboot.json");
#endif
    const auto config = android_quickboot_config(
            "hw.cpu.ncore=2\nfastboot.forceColdBoot=yes\nfastboot.forceFastBoot=no\n");
    assert(config == "hw.cpu.ncore=2\nfastboot.forceColdBoot=no\nfastboot.forceFastBoot=yes\n");
    assert(android_quickboot_config(config) == config);
    auto fresh_spec = spec;
    fresh_spec["env"]["VOYEMU_HOST_WINDOW_RECORD"] = "fresh/session/window.json";
    auto earlier_spec = spec;
    earlier_spec["env"]["VOYEMU_HOST_WINDOW_RECORD"] = "old/session/window.json";
    assert(android_quickboot_key(fresh_spec) == android_quickboot_key(earlier_spec));
    fresh_spec["gpuMode"] = "auto";
    assert(android_quickboot_key(fresh_spec) != android_quickboot_key(earlier_spec));
    auto host_spec = fresh_spec;
    host_spec["gpuMode"] = "host";
    auto headless_spec = fresh_spec;
    headless_spec["gpuMode"] = "auto-no-window";
    assert(android_quickboot_key(host_spec) != android_quickboot_key(fresh_spec) &&
           android_quickboot_key(headless_spec) != android_quickboot_key(fresh_spec));
    assert(android_quickboot_key(host_spec) != android_quickboot_key(headless_spec));
    // Removing host runtime state cannot remove installed data or resume a RAM checkpoint.
    const auto runtime = root / ".local/machines/test-machine/android";

    for (const char *name : {"cache.img", "cache.img.qcow2"}) {
        ensure_private_directory(runtime);
        std::ofstream(runtime / name) << "disposable cache";
    }

    AndroidQuickBoot separated(machine, key, runtime);
    separated.begin();
    const auto separated_snapshot = runtime / "snapshots" / AndroidQuickBoot::snapshot_name;

    for (const char *name : {"snapshot.pb", "ram.bin", "hardware.ini"}) {
        std::ofstream(separated_snapshot / name) << "host-only state";
    }

    separated.commit(boot);
    assert(separated.inspect().resume);
    const auto installed = sha256_file(machine / "userdata-qemu.img"),
               installed_overlay = sha256_file(machine / "userdata-qemu.img.qcow2");
    std::filesystem::remove_all(root / ".local/machines");
    assert(!separated.runtime_checkpoint().resume &&
           sha256_file(machine / "userdata-qemu.img") == installed &&
           sha256_file(machine / "userdata-qemu.img.qcow2") == installed_overlay);
    auto free_spec = spec;
    free_spec["wheel_capability"] =
            qinggan11_partial_wheel_capability("free-11-chinese-20260731-7.3.1");
    auto ru_spec = spec;
    ru_spec["wheel_capability"] =
            qinggan11_partial_wheel_capability("free-11-russian-20260318-rc1");
    // Preparation derives the capability from the wheel of a profile; only an invalid name fails.
    auto foreign_spec = spec;
    foreign_spec["wheel_capability"] = qinggan11_partial_wheel_capability("Foreign Profile");
    assert(android_presave_required(free_spec) && android_presave_required(ru_spec) &&
           !android_presave_required(foreign_spec) && !android_presave_required(spec));
    const auto free_key = android_quickboot_key(free_spec);
    assert(free_key.at("presave_protocol") == 1 && !key.contains("presave_protocol"));
    AndroidQuickBoot free_cache(machine, free_key);
    free_cache.begin();
    bool missing_receipt = false;
    try {
        free_cache.commit(boot);
    } catch (...) {
        missing_receipt = true;
    }

    assert(missing_receipt && !free_cache.runtime_checkpoint().resume);
    free_cache.commit(boot, session, *saved, guest, 7);
    assert(free_cache.inspect().resume && free_cache.runtime_checkpoint().resume);
    const auto record = machine / "voyemu-quickboot.json";
    const auto clean = read_private_json(record);
    assert(clean.at("presave").at("receipt").at("boot_id") == guest &&
           clean.at("presave").at("host_boot_nonce") == boot && guest != boot);

    for (const auto pointer :
         {"/presave/host_session_id", "/presave/host_boot_nonce", "/presave/receipt/session_id",
          "/presave/receipt/boot_id", "/presave/receipt/save_nonce"}) {
        auto corrupt = clean;
        corrupt[Json::json_pointer(pointer)] = "invalid";
        write_private_json(record, corrupt);
        assert(!free_cache.inspect().resume && !free_cache.runtime_checkpoint().resume);
    }

    auto wrong_boot = clean;
    wrong_boot["presave"]["receipt"]["boot_id"] = "11111111-1111-1111-1111-111111111111";
    write_private_json(record, wrong_boot);
    assert(!free_cache.inspect().resume && !free_cache.runtime_checkpoint().resume);
    auto wrong_sequence = clean;
    wrong_sequence["presave"]["receipt"]["sequence"] = 8;
    write_private_json(record, wrong_sequence);
    assert(!free_cache.inspect().resume && !free_cache.runtime_checkpoint().resume);
    const Json malformed_metadata = {{"/presave", nullptr},
                                     {"/presave/receipt", Json::array()},
                                     {"/presave/host_session_id", 7},
                                     {"/presave/host_boot_nonce", false},
                                     {"/presave/guest_boot_id", Json::object()},
                                     {"/presave/input_sequence", 7.5},
                                     {"/presave/receipt/schema", 1.0},
                                     {"/presave/receipt/sequence", 7.0},
                                     {"/presave/receipt/acoustic_zero", 1},
                                     {"/presave/receipt/save_nonce", nullptr}};

    for (const auto &[pointer, value] : malformed_metadata.items()) {
        auto corrupt = clean;
        corrupt[Json::json_pointer(pointer)] = value;
        write_private_json(record, corrupt);
        assert(!free_cache.inspect().resume && !free_cache.runtime_checkpoint().resume);
    }

    for (const char *field :
         {"host_session_id", "host_boot_nonce", "guest_boot_id", "input_sequence", "receipt"}) {
        auto corrupt = clean;
        corrupt["presave"].erase(field);
        write_private_json(record, corrupt);
        assert(!free_cache.inspect().resume && !free_cache.runtime_checkpoint().resume);
    }

    auto old = clean;
    old.erase("presave");
    write_private_json(record, old);
    assert(!free_cache.inspect().resume && !free_cache.runtime_checkpoint().resume);
    old = clean;
    old["key"].erase("presave_protocol");
    write_private_json(record, old);
    assert(!free_cache.inspect().resume && !free_cache.runtime_checkpoint().resume);
    write_private_json(record, clean);
    assert(AndroidQuickBoot(machine, free_key)
                   .inspect()
                   .resume); // A new host lifetime validates the origin binding, not its new
                             // session id.
    auto passion_spec = spec;
    passion_spec["wheel_capability"] = passion_wheel_partial_capability();
    assert(android_presave_required(passion_spec));
    const auto passion_key = android_quickboot_key(passion_spec);
    assert(passion_key.at("presave_protocol") == 1);
    AndroidQuickBoot passion_cache(machine, passion_key);
    passion_cache.begin();
    bool passion_missing = false;
    try {
        passion_cache.commit(boot);
    } catch (...) {
        passion_missing = true;
    }

    assert(passion_missing && !passion_cache.runtime_checkpoint().resume);
    passion_cache.commit(boot, session, *saved, guest, 7);
    assert(passion_cache.inspect().resume);
    auto not_zero = *saved;
    not_zero["acoustic_zero"] = false;
    passion_cache.begin();
    bool refused = false;
    try {
        passion_cache.commit(boot, session, not_zero, guest, 7);
    } catch (...) {
        refused = true;
    }

    assert(refused && !passion_cache.runtime_checkpoint().resume);
    passion_spec["wheel_capability"] = {{"schema", 1},
                                        {"profile", "passion-11-russian-20260403-2.1.4.80"},
                                        {"status", "unsupported"},
                                        {"supported", false},
                                        {"reason", "provider_not_installed"}};
    assert(!android_presave_required(passion_spec) &&
           !android_quickboot_key(passion_spec).contains("presave_protocol"));
#ifdef _WIN32
    {
        const auto runtime = root / "avd";
        AndroidQuickBoot linked(machine, key, runtime);
        linked.begin();

        for (const auto *name : {"cache.img", "cache.img.qcow2"}) {
            std::ofstream(runtime / name) << "disposable cache";
        }

        for (const auto *name : {"snapshot.pb", "ram.bin", "hardware.ini"}) {
            std::ofstream(runtime / "snapshots" / AndroidQuickBoot::snapshot_name / name)
                    << "saved snapshot";
        }

        for (const auto *name : {"encryptionkey.img", "system.img.qcow2", "vendor.img.qcow2"}) {
            std::filesystem::create_hard_link(machine / name, runtime / name);
        }

        for (const auto *name : {"system.img", "vendor.img"}) {
            std::filesystem::create_hard_link(machine / "base" / name, runtime / name);
        }

        linked.commit(boot);
        assert(linked.inspect().resume && linked.runtime_checkpoint().resume);
        const auto reject_link = [&](const auto &disk) {
            const auto foreign = root / "foreign-link.img";
            std::filesystem::create_hard_link(disk, foreign);
            assert(!linked.inspect().resume && !linked.runtime_checkpoint().resume);
            linked.begin();
            bool rejected = false;

            try {
                linked.commit(boot);
            } catch (const VoyemuError &error) {
                rejected = std::string_view(error.what()).starts_with("QUICKBOOT_FILE:");
            }

            assert(rejected);
            std::filesystem::remove(foreign);
            linked.commit(boot);
            assert(linked.inspect().resume);
        };
        reject_link(machine / "base/system.img");
        reject_link(machine / "userdata-qemu.img");
    }
#endif
    std::filesystem::remove_all(root);
    std::cout << "checkpoint invalidation and unchanged guest storage passed\n";
} catch (const std::exception &error) {
    std::cerr << error.what() << '\n';

    return 1;
}
