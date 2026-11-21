#include "qnx_ready_collector.hpp"
#include <cassert>
#include <fcntl.h>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

namespace {
using voyemu::QnxReadyCollector;
constexpr std::uint64_t base = 0x40000000, ring = 0x50010000, page = 0x40002000;
struct Fixture {
    std::filesystem::path dir, ram;

    Fixture() {
        dir = std::filesystem::path(".local/build/tmp") /
              ("qnx-ready-fixture-" + std::to_string(::getpid()) + "-" +
               std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directories(dir.parent_path());
        assert(std::filesystem::create_directory(dir));
        assert(::chmod(dir.c_str(), 0700) == 0);
        dir = std::filesystem::absolute(dir);
        ram = dir / "qnx-ram.bin";
        int fd = ::open(ram.c_str(), O_CREAT | O_EXCL | O_RDWR, 0600);
        assert(fd >= 0);
        assert(::ftruncate(fd, static_cast<off_t>(ring - base + 4096)) == 0);
        ::close(fd);
        write_page(page);
    }

    ~Fixture() {
        std::error_code error;
        std::filesystem::remove_all(dir, error);
    }

    void write_page(std::uint64_t address, std::uint32_t version = 3) {
        int fd = ::open(ram.c_str(), O_RDWR);
        assert(fd >= 0);
        std::array<std::uint32_t, 2> words{0x58464751, version};
        assert(::pwrite(fd, words.data(), sizeof(words), static_cast<off_t>(address - base)) ==
               sizeof(words));
        ::close(fd);
    }

    void incarnation(std::uint64_t pid, std::uint64_t nonce) {
        int fd = ::open(ram.c_str(), O_RDWR);
        assert(fd >= 0);
        std::array<std::uint64_t, 2> words{pid, nonce};
        assert(::pwrite(fd, words.data(), sizeof(words), page - base + 240) == sizeof(words));
        ::close(fd);
    }

    void legacy_ring(const std::string &line = "QG3 READY paddr=0x40002000 length=4096\n") {
        int fd = ::open(ram.c_str(), O_RDWR);
        assert(fd >= 0);
        std::array<std::uint32_t, 8> header{0xbeefeeee, 128, 0};
        assert(::pwrite(fd, header.data(), sizeof(header), ring - base) == sizeof(header));
        assert(::pwrite(fd, line.data(), line.size(), ring - base + 32) ==
               static_cast<ssize_t>(line.size()));
        ::close(fd);
    }
};

std::unique_ptr<QnxReadyCollector> collector(Fixture &f) {
    return std::make_unique<QnxReadyCollector>(f.ram, ring, 512);
}

std::vector<std::uint64_t> await(QnxReadyCollector &value) {
    for (unsigned i = 0; i < 40; ++i) {
        auto found = value.addresses();

        if (!found.empty()) {
            return found;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    return {};
}

template <class F> void rejects(F operation) {
    bool rejected = false;

    try {
        operation();
    } catch (const std::exception &) {
        rejected = true;
    }

    assert(rejected);
}

void await_error(QnxReadyCollector &value) {
    for (unsigned i = 0; i < 40; ++i) {
        value.addresses();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
}
} // namespace

int main() {
    try {
        {
            Fixture f;
            f.legacy_ring();
            auto c = collector(f);
            assert(await(*c) == std::vector<std::uint64_t>{page});
        }
        {
            Fixture f;
            f.incarnation(41, 100);
            f.legacy_ring("QG3 READY paddr=0x40002000 length=4096 pid=41 incarnation=0x64\n");
            auto c = collector(f);
            assert(await(*c) == std::vector<std::uint64_t>{page});
            assert((c->ready() == std::vector<QnxReadyCollector::Ready>{{page, 41, 100}}));
        }
        {
            Fixture f;
            f.incarnation(42, 101);
            f.legacy_ring("QG3 READY paddr=0x40002000 length=4096 pid=41 incarnation=0x64\n");
            auto c = collector(f);
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            assert(c->ready().empty());
        }
        {
            Fixture f;
            f.write_page(page, 2);
            f.legacy_ring();
            auto c = collector(f);
            rejects([&] {
                await_error(*c);
            });
        }
        {
            Fixture f;
            f.legacy_ring("QG3 READY paddr=0x30002000 length=4096\n");
            auto c = collector(f);
            rejects([&] {
                await_error(*c);
            });
        }
        {
            Fixture f;
            auto c = collector(f);
            std::filesystem::rename(f.ram, f.dir / "prior-ram");
            std::ofstream(f.ram).close();
            rejects([&] {
                c->addresses();
            });
        }
        std::cout << "legacy ring, owners, identity, bounds and RAM generation GREEN\n";

        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';

        return 1;
    }
}
