#include "os/platform.hpp"
#include "os/private_state_security.hpp"
#include "process.hpp"
#include "qnx_persistent_storage.hpp"
#include "qnx_process_ram.hpp"
#include "qnx_process_ram_test_host.hpp"
#include <fstream>
#include <iostream>
#include <map>
#include <winioctl.h>
using namespace voyemu;

static void create_owned_disk(const std::filesystem::path &path, std::uint64_t bytes,
                              std::string_view marker) {
    detail::PrivateStateSecurity security;
    const HANDLE file = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE,
                                    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                    &security.attributes, CREATE_NEW, FILE_ATTRIBUTE_NORMAL,
                                    nullptr);

    assert(file != INVALID_HANDLE_VALUE);
    DWORD returned = 0;
    assert(DeviceIoControl(file, FSCTL_SET_SPARSE, nullptr, 0, nullptr, 0, &returned, nullptr));
    LARGE_INTEGER end{};
    end.QuadPart = bytes;
    assert(SetFilePointerEx(file, end, nullptr, FILE_BEGIN) && SetEndOfFile(file));
    end.QuadPart = 0;
    assert(SetFilePointerEx(file, end, nullptr, FILE_BEGIN));
    assert(WriteFile(file, marker.data(), static_cast<DWORD>(marker.size()), &returned, nullptr) &&
           returned == marker.size());
    CloseHandle(file);
}

void diagnostic_persistent_storage_case() {
    const auto root = std::filesystem::canonical(std::filesystem::temp_directory_path()) /
                      ("voyemu-native-storage-" + random_token());

    ensure_private_directory(root);
    struct Cleanup {
        std::filesystem::path path;

        ~Cleanup() {
            std::filesystem::remove_all(path);
        }
    } cleanup{root};
    const auto system = root / "system", userdata = root / "userdata",
               engine = root / "native-qemu-fixture";
    {
        std::ofstream image(engine, std::ios::binary);
        const unsigned char header[] = {0xcf, 0xfa, 0xed, 0xfe, 12, 0, 0, 1};
        image.write(reinterpret_cast<const char *>(header), 8);
    }
    create_owned_disk(system, 1025ULL << 20, "installed");
    create_owned_disk(userdata, 32ULL << 20, "installed");
    const auto pin = sha256_file(engine);
    const auto mapping = qnx_persistent_ram_launch(QnxEngine::hvf, system.string(),
                                                   userdata.string());
    Json spec = {{"persistent_storage",
                  {{"schema", 1}, {"system", system.string()}, {"userdata", userdata.string()}}},
                 {"qemu", {{"executable", engine.string()}, {"args", mapping.options}}},
                 {"qmp", {{"host", "127.0.0.1"}, {"port", 45997}}},
                 {"pins", Json::array({{{"path", engine.string()}, {"sha256", pin}}})},
                 {"diagnostic_native",
                  {{"schema", 1},
                   {"executable", engine.string()},
                   {"launcher", "explicit-native-launcher"},
                   {"qmp_port", 45997},
                   {"persistent_backend", "macos-file-numa-v1"},
                   {"engine_sha256", pin}}}};
    assert(qnx_native_diagnostic_file_backend_supported(spec, pin));
    {
        QnxPersistentStorage storage(spec);
        storage.flush();
    }
    auto rejects = [](const Json &invalid) {
        bool rejected = false;
        try {
            QnxPersistentStorage storage(invalid);
        } catch (const std::exception &) {
            rejected = true;
        }

        assert(rejected);
    };
    auto bad = spec;
    bad.erase("diagnostic_native");
    rejects(bad);
    bad = spec;
    bad["diagnostic_native"]["engine_sha256"] = std::string(64, 'a');
    rejects(bad);
    bad = spec;
    bad["diagnostic_native"]["schema"] = 2;
    rejects(bad);
    bad = spec;
    bad["diagnostic_native"]["extra"] = true;
    rejects(bad);
    bad = spec;
    bad["qmp"]["host"] = "0.0.0.0";
    rejects(bad);
    bad = spec;
    bad["pins"].push_back(bad["pins"][0]);
    rejects(bad);
    bad = spec;
    bad["pins"][0]["sha256"] = std::string(64, 'b');
    rejects(bad);
    bad = spec;
    bad["qemu"]["args"][3] = "memory-backend-file,id=user,size=32M,mem-path=/foreign,share=on";
    rejects(bad);
    bad = spec;
    bad["diagnostic_native"]["schema"] = 1.0;
    rejects(bad);
    bad = spec;
    bad["qmp"]["port"] = "45997";
    bad["diagnostic_native"]["qmp_port"] = "45997";
    rejects(bad);
    bad = spec;
    bad["qmp"]["port"] = 1;
    bad["diagnostic_native"]["qmp_port"] = 1;
    rejects(bad);
    bad = spec;
    bad["diagnostic_native"]["persistent_backend"] = "wrong";
    bad["qnx_file_backend"] = {{"schema", 1}, {"version", "11.1.2"}, {"engine_sha256", pin}};
    rejects(bad);
    const auto pe = root / "pe-fixture";
    {
        std::ofstream image(pe, std::ios::binary);
        image << "MZ-not-a-native-engine";
    }
    const auto pe_pin = sha256_file(pe);
    bad = spec;
    bad["qemu"]["executable"] = pe.string();
    bad["diagnostic_native"]["executable"] = pe.string();
    bad["diagnostic_native"]["engine_sha256"] = pe_pin;
    bad["pins"] = Json::array({{{"path", pe.string()}, {"sha256", pe_pin}}});
    rejects(bad);

    {
        QnxPersistentStorage storage(spec);
        const auto retained = root / "retained-userdata";
        std::filesystem::rename(userdata, retained);
        create_owned_disk(userdata, 32ULL << 20, "installed");
        bool rejected = false;
        try {
            storage.flush();
        } catch (const std::exception &) {
            rejected = true;
        }

        assert(rejected);
        std::filesystem::remove(userdata);
        std::filesystem::rename(retained, userdata);
        storage.flush();
    }
    {
        std::ifstream state(userdata, std::ios::binary);
        std::string text(9, ' ');
        state.read(text.data(), 9);
        assert(text == "installed");
    }
}

void persistent_storage_case() {
    {
        const auto root = std::filesystem::canonical(std::filesystem::temp_directory_path()) /
                          ("voyemu-persistent-storage-" + random_token());
        ensure_private_directory(root);
        struct Cleanup {
            std::filesystem::path path;

            ~Cleanup() {
                std::filesystem::remove_all(path);
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
        const auto engine = root / "engine.bin", system = root / "system",
                   userdata = root / "userdata";
        {
            std::ofstream file(engine);
            file << "verified engine fixture";
        }

        for (const auto &disk :
             {std::pair{system, 1025ULL << 20}, std::pair{userdata, 32ULL << 20}}) {
            create_owned_disk(disk.first, disk.second, "installed");
        }

        const auto mapping = qnx_persistent_ram_launch(QnxEngine::tcg_file, system.string(),
                                                       userdata.string(), true);
        const auto engine_sha = sha256_file(engine);
        const Json capability = {
                {"schema", 1}, {"version", "11.1.2"}, {"engine_sha256", engine_sha}};
        Json spec = {
                {"persistent_storage",
                 {{"schema", 1}, {"system", system.string()}, {"userdata", userdata.string()}}},
                {"qnx_file_backend", capability},
                {"qemu", {{"executable", engine.string()}, {"args", mapping.options}}}};
        assert(qnx_file_backend_supported(capability, engine_sha));
        assert(!qnx_file_backend_supported(capability, std::string(64, "0"[0])));
        auto old = spec;
        old.erase("qnx_file_backend");
        rejects([&] {
            QnxPersistentStorage missing(old);
        });
        old = spec;
        old["qnx_file_backend"]["version"] = "11.1.0";
        rejects([&] {
            QnxPersistentStorage invalid(old);
        });
        old = spec;
        old["qnx_file_backend"]["engine_sha256"] = std::string(64, "0"[0]);
        rejects([&] {
            QnxPersistentStorage invalid(old);
        });
        const auto ram = root / "qnx-ram.bin";
        create_qnx_ram(ram, 8ULL << 30);
        assert(std::filesystem::file_size(ram) == 8ULL << 30);
        {
            const HANDLE file = CreateFileW(ram.c_str(), READ_CONTROL,
                                            FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                            OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
            assert(file != INVALID_HANDLE_VALUE);
            assert(detail::PrivateStateSecurity{}.accepts(file, true));
            CloseHandle(file);
        }
        rejects([&] {
            create_qnx_ram(ram, 8ULL << 30);
        });
        // Passion RAM has its own extent; no other extent is created.
        const auto passion_ram = root / "passion" / "qnx-ram.bin";
        ensure_private_directory(passion_ram.parent_path());
        rejects([&] {
            create_qnx_ram(passion_ram, (11713ULL << 20) - 4096);
        });
        assert(!std::filesystem::exists(passion_ram));
        create_qnx_ram(passion_ram, 11713ULL << 20);
        assert(std::filesystem::file_size(passion_ram) == 11713ULL << 20);
        std::filesystem::remove(passion_ram);
        QnxPersistentStorage storage(spec);
        storage.flush();
        {
            std::ifstream file(system);
            char word[9]{};
            file.read(word, 9);
            assert(std::string(word, 9) == "installed");
        }
        auto wrong = spec;
        wrong["qemu"]["args"][3] =
                "memory-backend-file,id=user,size=32M,mem-path=/foreign,share=on";
        rejects([&] {
            QnxPersistentStorage invalid(wrong);
        });
        std::filesystem::rename(userdata, root / "original-userdata");
        create_owned_disk(userdata, 32ULL << 20, "replacement");
        rejects([&] {
            storage.flush();
        });
        std::filesystem::remove(userdata);
        std::filesystem::rename(root / "original-userdata", userdata);
        storage.flush();
        std::filesystem::resize_file(userdata, 4096);
        rejects([&] {
            storage.flush();
        });
        rejects([&] {
            QnxPersistentStorage invalid(spec);
        });
        std::cout
                << "Win32 retained disk handles, replacement/extent guards, durable flush and exclusive sparse RAM passed\n";
    }
}
