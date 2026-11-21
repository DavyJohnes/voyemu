#include "qnx_process_ram.hpp"
#include "process.hpp"
#include "qnx_persistent_storage.hpp"
#include <fstream>
#include <map>
#ifdef _WIN32
#include "../core/private_state_security.hpp"
#include <winioctl.h>
#else
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#endif
#include <algorithm>
#include <cassert>
#include <iostream>
#include <stdexcept>
#include <string>
using namespace voyemu;
#ifdef _WIN32
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

static void diagnostic_persistent_storage() {
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
#endif

int main() try {
#ifdef _WIN32
    diagnostic_persistent_storage();
#endif
    const std::string engine_pin(64, "a"[0]);
    const Json engine_capability = {
            {"schema", 1}, {"version", "11.1.2"}, {"engine_sha256", engine_pin}};
    assert(qnx_file_backend_supported(engine_capability, engine_pin));

    for (const auto &invalid : {Json(), Json::array(), Json::object()}) {
        assert(!qnx_file_backend_supported(invalid, engine_pin));
    }

    auto invalid_capability = engine_capability;
    invalid_capability["schema"] = "1";
    assert(!qnx_file_backend_supported(invalid_capability, engine_pin));
    invalid_capability = engine_capability;
    invalid_capability["version"] = true;
    assert(!qnx_file_backend_supported(invalid_capability, engine_pin));
    invalid_capability = engine_capability;
    invalid_capability["engine_sha256"] = 7;
    assert(!qnx_file_backend_supported(invalid_capability, engine_pin));
    invalid_capability = engine_capability;
    invalid_capability["extra"] = true;
    assert(!qnx_file_backend_supported(invalid_capability, engine_pin));
    const auto tcg = qnx_ram_launch(QnxEngine::tcg_file), mac = qnx_ram_launch(QnxEngine::hvf);
    const std::vector<std::string> clock{"-icount", "shift=0,align=off,sleep=on"};
    const auto passion_mac = qnx_ram_launch(QnxEngine::hvf, 11713),
               passion_tcg = qnx_ram_launch(QnxEngine::tcg_file, 11713);
    std::vector<std::string> passion_tcg_options = passion_mac.options;
    passion_tcg_options.insert(passion_tcg_options.end(), clock.begin(), clock.end());
    assert(passion_mac.options ==
                   std::vector<std::string>(
                           {"-object",
                            "memory-backend-file,id=qnxmem,size=11713M,mem-path=${SESSION}/qnx-ram.bin,share=on"}) &&
           passion_tcg.options == passion_tcg_options);

    for (const auto memory : {0u, 8191u, 8193u, 11712u, 11714u}) {
        bool denied = false;
        try {
            qnx_ram_launch(QnxEngine::hvf, memory);
        } catch (const std::runtime_error &) {
            denied = true;
        }

        assert(denied);
    }

    assert(mac.backend == "file" && mac.machine.ends_with("memory-backend=qnxmem") &&
           mac.options ==
                   std::vector<std::string>(
                           {"-object",
                            "memory-backend-file,id=qnxmem,size=8192M,mem-path=${SESSION}/qnx-ram.bin,share=on"}));
    assert(tcg.accelerator == "tcg,thread=single" && mac.accelerator == "hvf,kernel-irqchip=on");
    assert(std::find(mac.options.begin(), mac.options.end(), "-icount") == mac.options.end());
    assert(qnx_ram_clock_options_current(QnxEngine::tcg_file,
                                         {"-cpu", "max", "-icount", "shift=0,align=off,sleep=on"}));

    for (const auto &stale :
         std::vector<std::vector<std::string>>{{},
                                               {"-icount"},
                                               {"-icount", "shift=auto"},
                                               {"-icount", "shift=0,align=off,sleep=on", "-icount",
                                                "shift=0,align=off,sleep=on"}}) {
        assert(!qnx_ram_clock_options_current(QnxEngine::tcg_file, stale));
    }

    assert(qnx_ram_clock_options_current(QnxEngine::hvf, {"-accel", "hvf,kernel-irqchip=on"}));
    assert(!qnx_ram_clock_options_current(QnxEngine::hvf,
                                          {"-icount", "shift=0,align=off,sleep=on"}));
    const auto persistent = qnx_persistent_ram_launch(
            QnxEngine::hvf, "/machines/car/qnx/system.img", "/machines/car/qnx/userdata.img");
    assert(persistent.backend == "file" &&
           persistent.machine.find("memory-backend") == std::string::npos &&
           persistent.options.size() == 20);
    assert(persistent.options[1] ==
           "memory-backend-file,id=low,size=1128M,mem-path=${SESSION}/qnx-ram.bin,offset=0,share=on");
    assert(persistent.options[3] ==
           "memory-backend-file,id=user,size=32M,mem-path=/machines/car/qnx/userdata.img,share=on");
    assert(persistent.options[5] ==
           "memory-backend-file,id=mid,size=888M,mem-path=${SESSION}/qnx-ram.bin,offset=1216348160,share=on");
    assert(persistent.options[7] ==
           "memory-backend-file,id=disk,size=1025M,mem-path=/machines/car/qnx/system.img,share=on");
    assert(persistent.options[9] ==
           "memory-backend-file,id=high,size=5119M,mem-path=${SESSION}/qnx-ram.bin,offset=3222274048,share=on");
    assert(persistent.options[11] == "node,nodeid=0,cpus=0-7,memdev=low" &&
           persistent.options[19] == "node,nodeid=4,memdev=high");
#ifdef _WIN32
    // The Windows engine maps the RAM file through its own file backend, which must be verified.
    bool unsupported = false;
    try {
        qnx_persistent_ram_launch(QnxEngine::tcg_file, "/system", "/userdata");
    } catch (const std::runtime_error &error) {
        unsupported = std::string(error.what()).starts_with("QNX_PERSISTENCE_UNSUPPORTED:");
    }

    assert(unsupported);
#endif
    const auto windows_persistent =
            qnx_persistent_ram_launch(QnxEngine::tcg_file, "/machines/car/qnx/system.img",
                                      "/machines/car/qnx/userdata.img", true);
    assert(windows_persistent.backend == "file" &&
           windows_persistent.accelerator == "tcg,thread=single" &&
           windows_persistent.options.size() == 22);
    assert(std::equal(persistent.options.begin(), persistent.options.end(),
                      windows_persistent.options.begin()));
    assert(windows_persistent.options[20] == "-icount" &&
           windows_persistent.options[21] == "shift=0,align=off,sleep=on");

    // Linux and Windows run TCG with the clock and keep the RAM file of the HVF launch.
    std::vector<std::string> tcg_options = mac.options;
    tcg_options.insert(tcg_options.end(), clock.begin(), clock.end());
    assert(tcg.backend == "file" && tcg.machine == mac.machine && tcg.options == tcg_options);
    assert(qnx_ram_clock_options_current(QnxEngine::tcg_file, tcg.options) &&
           !qnx_ram_clock_options_current(QnxEngine::tcg_file, mac.options));
    assert(std::string(qnx_engine_cpu(QnxEngine::hvf)) == "host" &&
           std::string(qnx_engine_cpu(QnxEngine::tcg_file)) == "max");

    for (const auto &path : {"", "/comma,path", "/newline\npath"}) {
        bool invalid = false;
        try {
            qnx_persistent_ram_launch(QnxEngine::hvf, path, "/userdata");
        } catch (const std::runtime_error &) {
            invalid = true;
        }

        assert(invalid);
    }

#ifndef _WIN32
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
        const auto system = root / "system", userdata = root / "userdata";

        for (const auto &disk :
             {std::pair{system, 1025ULL << 20}, std::pair{userdata, 32ULL << 20}}) {
            std::ofstream file(disk.first);
            file << "installed";
            file.close();
            std::filesystem::resize_file(disk.first, disk.second);
            std::filesystem::permissions(disk.first, std::filesystem::perms::owner_read |
                                                             std::filesystem::perms::owner_write);
        }

        const auto mapping = qnx_persistent_ram_launch(host_qnx_engine, system.string(),
                                                       userdata.string());
        Json spec = {
                {"persistent_storage",
                 {{"schema", 1}, {"system", system.string()}, {"userdata", userdata.string()}}},
                {"qemu", {{"args", mapping.options}}}};
        auto rejects = [](const auto &action) {
            bool rejected = false;
            try {
                action();
            } catch (const std::exception &) {
                rejected = true;
            }

            assert(rejected);
        };
        const auto ram = root / "qnx-ram.bin";
        create_qnx_ram(ram, 8ULL << 30);
        assert(std::filesystem::file_size(ram) == 8ULL << 30);
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
        // Exercise the real OS mappings described by production argv, not a second layout formula.
        std::uint64_t mapped_bytes = 0;
        std::map<std::string, std::uint64_t> node_bytes;

        for (std::size_t i = 0; i < mapping.options.size(); i += 2) {
            if (mapping.options[i] == "-object") {
                std::map<std::string, std::string> fields;
                auto text = mapping.options[i + 1];
                std::size_t position = text.find(',') + 1;

                while (position < text.size()) {
                    auto end = text.find(',', position);

                    if (end == std::string::npos) {
                        end = text.size();
                    }

                    auto field = text.substr(position, end - position);
                    auto equal = field.find('=');
                    assert(equal != std::string::npos);
                    fields[field.substr(0, equal)] = field.substr(equal + 1);
                    position = end + 1;
                }

                const auto bytes = std::stoull(fields.at("size")) * 1024 * 1024,
                           offset = fields.contains("offset") ? std::stoull(fields.at("offset"))
                                                              : 0;
                auto path = fields.at("mem-path");

                if (path == "${SESSION}/qnx-ram.bin") {
                    path = ram.string();
                }

                const auto extent = std::filesystem::file_size(path);
                assert(offset <= extent && bytes <= extent - offset);
                const int fd = ::open(path.c_str(), O_RDWR);
                assert(fd >= 0);
                auto *memory = static_cast<unsigned char *>(
                        ::mmap(nullptr, bytes, PROT_READ | PROT_WRITE, MAP_SHARED, fd,
                               static_cast<off_t>(offset)));
                assert(memory != MAP_FAILED);
                memory[bytes - 1] = 0x5a;
                assert(::msync(memory, bytes, MS_SYNC) == 0);
                assert(::munmap(memory, bytes) == 0);
                ::close(fd);
                mapped_bytes += bytes;
                node_bytes.emplace(fields.at("id"), bytes);
            }
        }

        assert(mapped_bytes == 8ULL << 30 && node_bytes.size() == 5);
        unsigned nodes = 0;

        for (std::size_t i = 0; i < mapping.options.size(); i += 2) {
            if (mapping.options[i] == "-numa") {
                const auto &node = mapping.options[i + 1];
                const auto id = node.substr(node.find("memdev=") + 7);
                assert(node_bytes.erase(id) == 1);
                ++nodes;
            }
        }

        assert(nodes == 5 && node_bytes.empty());
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
        {
            std::ofstream file(userdata);
            file << "replacement";
        }
        std::filesystem::resize_file(userdata, 32ULL << 20);
        std::filesystem::permissions(userdata, std::filesystem::perms::owner_read |
                                                       std::filesystem::perms::owner_write);
        rejects([&] {
            storage.flush();
        });
        std::filesystem::remove(userdata);
        std::filesystem::rename(root / "original-userdata", userdata);
        storage.flush();
        std::filesystem::resize_file(userdata, 4096);
        rejects([&] {
            QnxPersistentStorage invalid(spec);
        });
        std::filesystem::remove(userdata);
        std::filesystem::create_symlink(system, userdata);
        rejects([&] {
            QnxPersistentStorage invalid(spec);
        });
    }
#endif
#ifdef _WIN32
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
#endif
} catch (const std::exception &error) {
    std::cerr << error.what() << '\n';

    return 1;
}
