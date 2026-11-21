#include "os/platform.hpp"
#include "process.hpp"
#include "qnx_persistent_storage.hpp"
#include "qnx_process_ram.hpp"
#include "qnx_process_ram_test_host.hpp"
#include <fcntl.h>
#include <fstream>
#include <map>
#include <sys/mman.h>
#include <unistd.h>
using namespace voyemu;

void diagnostic_persistent_storage_case() {}

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
}
