#include "qnx_checkpoint.hpp"
#include "os/platform.hpp"
#include "process.hpp"
#include "qnx_interguest_contract.hpp"
#include "quickboot.hpp"
#include <algorithm>
#include <array>
#include <cassert>
#include <exception>
#include <fstream>
#include <iostream>
using namespace voyemu;

static void shared_ram_contract() {
    auto root = std::filesystem::canonical(std::filesystem::temp_directory_path()) /
                ("voyemu-shared-qnx-" + random_token());

    ensure_private_directory(root);
    ensure_private_directory(root / "gpu");
    ensure_private_directory(root / "session");
    struct Cleanup {
        std::filesystem::path p;

        ~Cleanup() {
            std::filesystem::remove_all(p);
        }
    } cleanup{root};
    auto rejects = [](const auto &action) {
        bool rejected = false;
        try {
            action();
        } catch (const std::exception &) {
            rejected = true;
        }

        assert(rejected);
    };
    const auto ram = root / "ram.bin", target = root / "session/qnx-ram.bin";
    {
        std::ofstream f(ram, std::ios::binary);
        f << "ORIGINAL";
        f.seekp(8192);
        f << "SECOND__";
        f.seekp(16384);
        f << "OUTSIDE_";
    }
    std::filesystem::resize_file(ram, 8ULL * 1024 * 1024 * 1024);
    std::filesystem::permissions(ram, std::filesystem::perms::owner_read |
                                              std::filesystem::perms::owner_write);
    {
        std::ofstream f(root / "vmstate.bin", std::ios::binary);
        const char bytes[] = {0x51, 0x45, 0x56, 0x4d, 0, 0, 0, 3};
        f.write(bytes, 8);
    }
    {
        std::ofstream f(root / "gpu/40001000.gpu", std::ios::binary);
        std::array<std::uint64_t, 3> header{0x3130305550475156ULL, 0x40001000ULL,
                                            8ULL * 1024 * 1024 * 1024};
        f.write(reinterpret_cast<const char *>(header.data()), 24);
    }

    for (const auto &file : {root / "vmstate.bin", root / "gpu/40001000.gpu"}) {
        std::filesystem::permissions(file, std::filesystem::perms::owner_read |
                                                   std::filesystem::perms::owner_write);
    }

    const std::string roms =
            "/rom@etc/acpi/tables size=0x200000 name=\"etc/acpi/tables\"\naddr=0000000040000000 size=0x8 mem=ram name=\"dtb\"\naddr=0000000040002000 size=0x8 mem=ram name=\"kernel\"\n";
    auto ranges = qnx_checkpoint_boot_roms(roms);
    assert(ranges.size() == 2 && ranges[1]["address"] == 0x40002000ULL);

    for (const auto &bad :
         {std::string{}, roms + "unknown\n",
          std::string("addr=fffffffffffffffff size=0x8 mem=ram name=\"overflow\"\n"),
          std::string("addr=240000000 size=0x8 mem=ram name=\"outside\"\n")}) {
        rejects([&] {
            qnx_checkpoint_boot_roms(bad);
        });
    }

    auto cold = Json{
            {"guest_arch", "aarch64"},
            {"graphics", {{"executable", "/worker"}, {"angle", "/angle"}}},
            {"pins", Json::array()},
            {"ram", "${SESSION}/qnx-ram.bin"},
            {"qemu",
             {{"args",
               Json::array(
                       {"-machine", "virt-11.0,memory-backend=qnxmem", "-m", "8192", "-object",
                        "memory-backend-file,id=qnxmem,size=8192M,mem-path=${SESSION}/qnx-ram.bin,share=on",
                        "-S"})}}},
            {"quickboot", {{"directory", root.string()}}}};
    const auto loader_file = root / "boot-input.bin";
    {
        std::ofstream f(loader_file);
        f << std::string(32, 'L');
    }
    cold["pins"].push_back({{"path", loader_file.string()}, {"sha256", sha256_file(loader_file)}});
    cold["qemu"]["args"].push_back("-device");
    cold["qemu"]["args"].push_back("loader,file=" + loader_file.string() +
                                   ",addr=0x50000000,force-raw=on");
    const auto cold_roms = roms + "addr=0000000050000000 size=0x20 mem=ram name=\"" +
                           loader_file.string() + "\"\n";
    assert(qnx_checkpoint_boot_roms(cold_roms, cold) == ranges);
    rejects([&] {
        qnx_checkpoint_boot_roms(roms, cold);
    });
    rejects([&] {
        qnx_checkpoint_boot_roms(cold_roms + "unknown\n", cold);
    });
    auto pin = [](const auto &file) {
        return Json{{"bytes", std::filesystem::file_size(file)}, {"sha256", sha256_file(file)}};
    };
    auto manifest =
            Json{{"schema", 1},
                 {"state", "ready"},
                 {"key", qnx_checkpoint_key(cold, "car-a")},
                 {"state_sequence", 1},
                 {"theme_sequence", 0},
                 {"wheel_sequence", 0},
                 {"held_keys", 0},
                 {"producers", Json::array({0x40001000ULL})},
                 {"vm", pin(root / "vmstate.bin")},
                 {"gpu", {{"40001000.gpu", pin(root / "gpu/40001000.gpu")}}},
                 {"memory",
                  {{"format", "shared-file-cpr-v1"}, {"ram", pin(ram)}, {"boot_roms", ranges}}}};
    write_private_json(root / "manifest.json", manifest);
    auto saved = inspect_qnx_checkpoint(cold, "car-a");
    assert(saved.resume && saved.external_ram && saved.launch["qemu"]["args"].back() == "defer" &&
           saved.boot_roms == ranges);
    auto network_cold = cold;
    network_cold["interguest_network"] = qnx_interguest_contract();
    auto network_manifest = manifest;
    network_manifest["key"] = qnx_checkpoint_key(network_cold, "car-a");
    write_private_json(root / "manifest.json", network_manifest);

    for (const auto &skipped : {inspect_qnx_checkpoint(network_cold, "car-a"),
                                runtime_qnx_checkpoint(network_cold, "car-a")}) {
        assert(!skipped.resume && skipped.reason == "network_v1_cold_only");
    }

    auto persistent_cold = cold;
    persistent_cold["persistent_storage"] = {
            {"schema", 1}, {"system", "/machine/system"}, {"userdata", "/machine/userdata"}};

    for (const auto &skipped : {inspect_qnx_checkpoint(persistent_cold, "car-a"),
                                runtime_qnx_checkpoint(persistent_cold, "car-a")}) {
        assert(!skipped.resume && skipped.reason == "persistent_storage_cold_only" &&
               skipped.launch == persistent_cold);
    }

    write_private_json(root / "manifest.json", manifest);
    saved = inspect_qnx_checkpoint(cold, "car-a");
    assert(remember_qnx_checkpoint_verification(saved));
    saved = inspect_qnx_checkpoint(cold, "car-a", true);
    assert(saved.resume && saved.verification == "cached_identity" &&
           saved.verified_files["files"].contains("ram.bin"));
    const auto base_identity = cache_file_identity(ram);
    auto clone_identity = clone_qnx_checkpoint_ram(saved, target);
    assert(clone_identity == cache_file_identity(target));
    rejects([&] {
        clone_qnx_checkpoint_ram(saved, target);
    });
    {
        std::fstream f(target, std::ios::binary | std::ios::in | std::ios::out);
        f << "BOOTBOOT";
        f.seekp(8192);
        f << "BOOTBOOT";
        f.seekp(16384);
        f << "NEWOTHER";
    }
    rejects([&] {
        restore_qnx_checkpoint_boot_roms(saved, target, roms + "unknown\n");
    });
    restore_qnx_checkpoint_boot_roms(saved, target, roms);
    {
        std::ifstream f(target, std::ios::binary);
        char bytes[8];
        f.read(bytes, 8);
        assert(std::string(bytes, 8) == "ORIGINAL");
        f.seekg(8192);
        f.read(bytes, 8);
        assert(std::string(bytes, 8) == "SECOND__");
        f.seekg(16384);
        f.read(bytes, 8);
        assert(std::string(bytes, 8) == "NEWOTHER");
    }
    assert(cache_file_identity(ram) == base_identity);
    std::filesystem::resize_file(target, 64);
    rejects([&] {
        restore_qnx_checkpoint_boot_roms(saved, target, roms);
    });
    std::filesystem::remove(target);
    std::filesystem::create_symlink(ram, target);
    rejects([&] {
        clone_qnx_checkpoint_ram(saved, target);
    });
    std::filesystem::remove(target);
    std::filesystem::create_directory_symlink(root / "session", root / "alias");
    rejects([&] {
        clone_qnx_checkpoint_ram(saved, root / "alias/qnx-ram.bin");
    });
    assert(!std::filesystem::exists(target));
    auto invalid = manifest;
    invalid["memory"]["boot_roms"][1]["address"] = 0x40000004ULL;
    write_private_json(root / "manifest.json", invalid);
    assert(!inspect_qnx_checkpoint(cold, "car-a", true).resume);
    invalid = manifest;
    invalid["memory"]["ram"]["bytes"] = 64;
    write_private_json(root / "manifest.json", invalid);
    assert(!inspect_qnx_checkpoint(cold, "car-a", true).resume);
    write_private_json(root / "manifest.json", manifest);
    auto modified = std::filesystem::last_write_time(ram);
    {
        std::fstream f(ram, std::ios::binary | std::ios::in | std::ios::out);
        f.put('X');
    }
    std::filesystem::last_write_time(ram, modified);
    rejects([&] {
        clone_qnx_checkpoint_ram(saved, target);
    });
    assert(!std::filesystem::exists(target));
    assert(!inspect_qnx_checkpoint(cold, "car-a", true).resume);
}

int main() try {
    // The shared RAM contract of the macOS engine.
    if (voyemu::os::platform_name == "macOS") {
        shared_ram_contract();
    }

    const auto root = std::filesystem::canonical(std::filesystem::temp_directory_path()) /
                      ("voyemu-qnx-cache-" + random_token());
    ensure_private_directory(root);
    ensure_private_directory(root / "gpu");
    struct Cleanup {
        std::filesystem::path p;

        ~Cleanup() {
            std::filesystem::remove_all(p);
        }
    } cleanup{root};
    auto cold = Json{{"schema", 1},
                     {"graphics", {{"executable", "/worker-a"}, {"angle", "/angle"}}},
                     {"qemu", {{"args", Json::array({"-S"})}}},
                     {"pins", Json::array({{{"path", "/worker-a"}, {"sha256", "first"}},
                                           {{"path", "/image"}, {"sha256", "guest"}}})},
                     {"quickboot", {{"directory", root.string()}}}};
    auto key = qnx_checkpoint_key(cold, "car-a");
    auto newer = cold;
    newer["graphics"]["executable"] = "/worker-b";
    newer["pins"][0] = {{"path", "/worker-b"}, {"sha256", "second"}};
    assert(qnx_checkpoint_key(newer, "car-a") == key);
    assert(qnx_checkpoint_key(cold, "car-b") != key);
    assert(inspect_qnx_checkpoint(cold, "car-a").reason == "cache_missing");
    {
        std::ofstream f(root / "vmstate.bin", std::ios::binary);
        const char data[] = {0x51, 0x45, 0x56, 0x4d, 0, 0, 0, 3, 0x58};
        f.write(data, 9);
    }
    auto write_gpu = [&] {
        std::ofstream f(root / "gpu/40001000.gpu", std::ios::binary);
        std::array<std::uint64_t, 3> words{0x3130305550475156ULL, 0x40001000ULL,
                                           8ULL * 1024 * 1024 * 1024};
        f.write(reinterpret_cast<const char *>(words.data()), 24);
    };
    write_gpu();

    for (const auto &p : {root / "vmstate.bin", root / "gpu/40001000.gpu"}) {
        std::filesystem::permissions(p, std::filesystem::perms::owner_read |
                                                std::filesystem::perms::owner_write);
    }

    auto pin = [&](const std::filesystem::path &p) {
        return Json{{"bytes", std::filesystem::file_size(p)}, {"sha256", sha256_file(p)}};
    };
    auto manifest = Json{{"schema", 1},
                         {"state", "ready"},
                         {"key", key},
                         {"state_sequence", 10},
                         {"theme_sequence", 2},
                         {"wheel_sequence", 0},
                         {"held_keys", 0},
                         {"producers", Json::array({0x40001000ULL})},
                         {"vm", pin(root / "vmstate.bin")},
                         {"gpu", {{"40001000.gpu", pin(root / "gpu/40001000.gpu")}}}};
    auto save = [&](Json value) {
        write_private_json(root / "manifest.json", value);
    };
    save(manifest);
    auto ready = inspect_qnx_checkpoint(cold, "car-a");
    assert(ready.resume && ready.state_sequence == 10 && ready.producers.size() == 1);
    assert(ready.launch["qemu"]["args"].back() == "file:" + (root / "vmstate.bin").string());
    assert(ready.launch["graphics"]["replay"] == (root / "gpu").string());
    const auto runtime_ready = runtime_qnx_checkpoint(cold, "car-a");
    assert(runtime_ready.resume && runtime_ready.verification == "configuration" &&
           runtime_ready.launch == ready.launch);
    {
        std::fstream changed(root / "vmstate.bin", std::ios::binary | std::ios::in | std::ios::out);
        changed.seekp(8);
        changed.put('Y');
    }
    assert(!inspect_qnx_checkpoint(cold, "car-a").resume &&
           runtime_qnx_checkpoint(cold, "car-a")
                   .resume); // Ordinary start does not read/hash image content.
    {
        std::fstream original(root / "vmstate.bin",
                              std::ios::binary | std::ios::in | std::ios::out);
        original.seekp(8);
        original.put('X');
    }
    assert(!runtime_qnx_checkpoint(cold, "car-b").resume);
    {
        const auto raw = root / "raw.bin";
        {
            std::ofstream f(raw, std::ios::binary);
            f << std::string(32, 'x');
        }
        auto loaders = cold;
        loaders["pins"].push_back({{"path", raw.string()}, {"sha256", sha256_file(raw)}});
        const std::string good = "loader,file=" + raw.string() + ",addr=0x86000000,force-raw=on";
        const std::string cpu = "loader,file=" + raw.string() +
                                ",addr=0x40200000,force-raw=on,cpu-num=0";
        const std::string outside = "loader,file=" + raw.string() +
                                    ",addr=0x240000000,force-raw=on";
        const std::string unknown = good + ",unknown=1",
                          data = "loader,addr=0x86000000,data=1,data-len=4";
        loaders["qemu"]["args"] =
                Json::array({"-machine", "virt-11.0,memory-backend=qnxmem", "-m", "8192", "-object",
                             "memory-backend-file,id=qnxmem,size=8192M,mem-path=" +
                                     (root / "ram.bin").string() + ",share=on",
                             "-S", "-device", good, "-device", cpu, "-device", outside, "-device",
                             unknown, "-device", data});
        loaders["ram"] = (root / "ram.bin").string();
        const auto original = loaders;
        auto saved = manifest;
        saved["key"] = qnx_checkpoint_key(loaders, "car-a");
        save(saved);
        const auto optimized = inspect_qnx_checkpoint(loaders, "car-a");
        assert(optimized.resume);
        assert(optimized.omitted_loaders == 2 && optimized.omitted_loader_bytes == 64);
        const auto &arguments = optimized.launch["qemu"]["args"];

        if (std::find(arguments.begin(), arguments.end(), good) != arguments.end()) {
            std::cerr << "validated warm restore still reads initial RAM loader images\n";

            return 1;
        }

        assert(loaders == original &&
               std::find(arguments.begin(), arguments.end(), cpu) == arguments.end());

        for (const auto &retained : {outside, unknown, data}) {
            assert(std::find(arguments.begin(), arguments.end(), retained) != arguments.end());
        }

        auto incompatible = loaders;
        incompatible["qemu"]["args"][1] = "other-board";
        const auto rejected = inspect_qnx_checkpoint(incompatible, "car-a");
        assert(!rejected.resume && rejected.launch == incompatible);
        save(manifest);
        ready = inspect_qnx_checkpoint(cold, "car-a");
    }
    {
        auto bad = manifest;
        bad["memory"] = {{"format", "future-memory-format"}};
        save(bad);

        if (inspect_qnx_checkpoint(cold, "car-a").resume) {
            std::cerr << "unknown checkpoint memory format was silently ignored\n";

            return 1;
        }

        save(manifest);
        ready = inspect_qnx_checkpoint(cold, "car-a");
    }
    assert(ready.verification == "full_sha256" &&
           !std::filesystem::exists(root / "verified-files.json"));
    assert(remember_qnx_checkpoint_verification(ready));
    auto cached = inspect_qnx_checkpoint(cold, "car-a", true);
    assert(cached.resume && cached.verification == "cached_identity");
    const auto receipt = read_private_json(root / "verified-files.json");
    const auto receipt_hash = sha256_file(root / "verified-files.json");
    const auto receipt_identity = cache_file_identity(root / "verified-files.json");
    assert(inspect_qnx_checkpoint(cold, "car-a").verification == "full_sha256" &&
           sha256_file(root / "verified-files.json") == receipt_hash &&
           cache_file_identity(root / "verified-files.json") == receipt_identity);
    assert(inspect_qnx_checkpoint(cold, "car-a", true).verification == "cached_identity" &&
           cache_file_identity(root / "verified-files.json") ==
                   receipt_identity); // Default/deep inspection remains read-only and hashes every
                                      // file.
    auto unrelated = receipt;
    unrelated["manifest_sha256"] = std::string(64, '0');
    write_private_json(root / "verified-files.json", unrelated);
    assert(inspect_qnx_checkpoint(cold, "car-a", true).verification == "full_sha256");
    write_private_json(root / "verified-files.json", receipt);
    const auto old_time = std::filesystem::last_write_time(root / "vmstate.bin");
    {
        std::fstream file(root / "vmstate.bin", std::ios::in | std::ios::out | std::ios::binary);
        file.seekp(8);
        file.put('Y');
    }
    std::filesystem::last_write_time(root / "vmstate.bin", old_time);
    assert(!inspect_qnx_checkpoint(cold, "car-a", true).resume &&
           !remember_qnx_checkpoint_verification(ready));
    {
        std::fstream file(root / "vmstate.bin", std::ios::in | std::ios::out | std::ios::binary);
        file.seekp(8);
        file.put('X');
    }
    ready = inspect_qnx_checkpoint(cold, "car-a", true);
    assert(ready.resume && ready.verification == "mixed");
    assert(remember_qnx_checkpoint_verification(ready) &&
           inspect_qnx_checkpoint(cold, "car-a", true).verification == "cached_identity");
    ready = inspect_qnx_checkpoint(cold, "car-a");
    assert(remember_qnx_checkpoint_verification(ready));

    // Creating a symbolic link needs a privilege on Windows.
    if (!voyemu::os::host_windows) {
        std::filesystem::rename(root / "verified-files.json", root / "receipt.saved");
        std::filesystem::create_symlink("receipt.saved", root / "verified-files.json");
        assert(inspect_qnx_checkpoint(cold, "car-a", true).resume &&
               inspect_qnx_checkpoint(cold, "car-a", true).verification == "full_sha256");
        bool linked_rejected = false;
        try {
            remember_qnx_checkpoint_verification(inspect_qnx_checkpoint(cold, "car-a"));
        } catch (...) {
            linked_rejected = true;
        }

        assert(linked_rejected);
        std::filesystem::remove(root / "verified-files.json");
        std::filesystem::rename(root / "receipt.saved", root / "verified-files.json");
    }

    for (const auto &patch :
         {Json{{"state_sequence", 0xfffffffeULL}}, Json{{"held_keys", 1}},
          Json{{"wheel_sequence", 1}}, Json{{"producers", Json::array({0x40001001ULL})}},
          Json{{"producers", Json::array({0x40001000ULL, 0x40001000ULL})}}}) {
        auto bad = manifest;
        bad.update(patch);
        save(bad);
        assert(!inspect_qnx_checkpoint(cold, "car-a", true).resume &&
               !remember_qnx_checkpoint_verification(ready));
    }

    save(manifest);
    assert(!inspect_qnx_checkpoint(cold, "car-b").resume);
    newer = cold;
    newer["pins"][1]["sha256"] = "changed";
    assert(!inspect_qnx_checkpoint(newer, "car-a").resume);
    { std::ofstream(root / "gpu/40001000.gpu", std::ios::app) << "corrupted"; }
    assert(!inspect_qnx_checkpoint(cold, "car-a").resume);
    write_gpu();
    ready = inspect_qnx_checkpoint(cold, "car-a");
    assert(ready.resume);
    reject_qnx_checkpoint(ready, "gpu_worker_exit", true);
    assert(!std::filesystem::exists(root / "rejected.json") &&
           inspect_qnx_checkpoint(cold, "car-a", true).resume);
    reject_qnx_checkpoint(ready, "gpu_worker_exit");
    assert(inspect_qnx_checkpoint(cold, "car-a", true).reason == "checkpoint_quarantined");
    manifest["theme_sequence"] = 3;
    save(manifest);
    assert(inspect_qnx_checkpoint(cold, "car-a").resume);

    // Creating a symbolic link needs a privilege on Windows.
    if (!voyemu::os::host_windows) {
        std::filesystem::rename(root / "gpu/40001000.gpu", root / "gpu/original.gpu");
        std::filesystem::create_symlink("original.gpu", root / "gpu/40001000.gpu");
        assert(!inspect_qnx_checkpoint(cold, "car-a").resume);
    }

    assert(!qnx_checkpoint_migration_ready({{"status", "active"}},
                                           {{"status", "paused"}, {"running", false}}));
    assert(qnx_checkpoint_migration_ready({{"status", "completed"}},
                                          {{"status", "paused"}, {"running", false}}));
} catch (const std::exception &error) {
    std::cerr << error.what() << '\n';

    return 1;
}
