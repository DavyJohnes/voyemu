#include "qnx_serial_ready.hpp"
#include "qnx_serial_lines.hpp"
#include <chrono>
#include <cstring>
#include <iostream>
#include <map>
#include <sstream>
#include <stdexcept>
using namespace voyemu;
namespace {
constexpr std::uint64_t base = 0x40000000, page = 0x40002000, free_end = 0x240000000ULL,
                        passion_end = 0x180000000ULL;
using Ready = QnxSerialReady::Ready;

void check(bool value, const char *why) {
    if (!value) {
        throw std::runtime_error(why);
    }
}

template <class F> void rejects(F operation, const char *why) {
    bool rejected = false;

    try {
        operation();
    } catch (const std::exception &) {
        rejected = true;
    }

    check(rejected, why);
}

// The session UART file and a sparse guest RAM read as the session reads them.
struct Guest {
    std::filesystem::path serial;
    std::uint64_t ram_end = free_end;
    std::map<std::uint64_t, std::array<std::byte, 4096>> pages;
    QnxSerialLines lines;
    QnxSerialReady ready;

    explicit Guest(const std::filesystem::path &directory) : serial(directory / "qnx-serial.log") {
        std::filesystem::remove(serial);
        write_page(page);
    }

    void write(std::uint64_t address, const void *bytes, std::size_t size) {
        auto &target = pages[address & ~std::uint64_t{4095}];
        std::memcpy(target.data() + (address & 4095), bytes, size);
    }

    void write_page(std::uint64_t address, std::uint32_t version = 3) {
        const std::array<std::uint32_t, 2> words{0x58464751, version};
        write(address, words.data(), sizeof(words));
    }

    void owner(std::uint64_t address, std::uint64_t pid, std::uint64_t incarnation) {
        const std::array<std::uint64_t, 2> words{pid, incarnation};
        write(address + 240, words.data(), sizeof(words));
    }

    void append(const std::string &bytes) {
        std::ofstream output(serial, std::ios::binary | std::ios::app);
        output << bytes;
        check(output.good(), "fixture write");
    }

    std::vector<Ready> poll(std::uint64_t mailbox_end = free_end) {
        ready.add(lines.read(serial));

        return ready.ready(
                [&](std::uint64_t address, std::span<std::byte> output) -> std::size_t {
                    if (address < base || address + output.size() > ram_end) {
                        return 0;
                    }

                    const auto found = pages.find(address & ~std::uint64_t{4095});

                    if (found == pages.end()) {
                        std::fill(output.begin(), output.end(), std::byte{});
                    } else {
                        std::memcpy(output.data(), found->second.data() + (address & 4095),
                                    output.size());
                    }

                    return output.size();
                },
                mailbox_end);
    }
};

std::string marker(std::uint64_t address) {
    std::ostringstream text;

    text << "QG3 READY paddr=0x" << std::hex << address << " length=4096\n";

    return text.str();
}

std::string slog2() {
    return "Jan 01 00:00:08.664        ivi_compositor.16318491               stderr      0  ";
}
} // namespace

int main() {
    try {
        const auto directory =
                std::filesystem::canonical(std::filesystem::temp_directory_path()) /
                ("qnx-serial-ready-" +
                 std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directory(directory);
        {
            Guest guest(directory);
            check(guest.poll().empty(), "no UART file, no producer");
            guest.append("noise\n" + std::string(4096, 'x') +
                         "\nQG3 READY paddr=0x40002000 length=4096\r\n" + marker(page));
            check(guest.poll() == std::vector<Ready>{{page, 0, 0}},
                  "marker after noise and an overlong line, CRLF, duplicate announcement");
            check(guest.poll() == std::vector<Ready>{{page, 0, 0}}, "accepted page retained");
        }
        {
            Guest guest(directory);
            guest.append("QG3 READY paddr=0x40002000 ");
            check(guest.poll().empty(), "partial marker line waits");
            guest.append("length=4096\n");
            check(guest.poll() == std::vector<Ready>{{page, 0, 0}}, "split marker line joined");
        }

        for (const auto *line :
             {"QG3 READY paddr=0x40002001 length=4096\n",
              "QG3 READY paddr=0x30002000 length=4096\n",
              "QG3 READY paddr=0x240002000 length=4096\n",
              "QG3 READY paddr=0x40002000 length=8192\n",
              "QG3 READY paddr=0x40002000 length=4096 pid=7 incarnation=0x0\n"}) {
            Guest guest(directory);
            guest.append(line);
            rejects(
                    [&] {
                        guest.poll();
                    },
                    "unaligned, out of range, wrong length or zero marker");
        }

        {
            Guest guest(directory);
            guest.owner(page, 41, 100);
            guest.append("QG3 READY paddr=0x40002000 length=4096 pid=41 incarnation=0x64\n");
            check(guest.poll() == std::vector<Ready>{{page, 41, 100}}, "owned marker");
            guest.owner(page, 41, 101);
            guest.append("QG3 READY paddr=0x40002000 length=4096 pid=41 incarnation=0x64\n"
                         "QG3 READY paddr=0x40002000 length=4096 pid=41 incarnation=0x65\n");
            check(guest.poll() == std::vector<Ready>{{page, 41, 101}},
                  "a retired announcement is skipped, the current incarnation replaces it");
            guest.append(marker(page));
            check(guest.poll() == std::vector<Ready>{{page, 41, 101}},
                  "an unbound marker does not replace an owned page");
        }
        {
            Guest guest(directory);
            guest.owner(page, 42, 101);
            guest.append("QG3 READY paddr=0x40002000 length=4096 pid=41 incarnation=0x64\n");
            check(guest.poll().empty(), "foreign owner skipped");
        }
        {
            Guest guest(directory);
            guest.ram_end = page;
            guest.append("QG3 READY paddr=0x40002000 length=4096 pid=41 incarnation=0x64\n");
            check(guest.poll().empty(), "unreadable owned page skipped");
            guest.append(marker(page));
            rejects(
                    [&] {
                        guest.poll();
                    },
                    "unreadable unbound page");
        }

        // Passion's graphical mailbox is 4 GiB; its final complete page is valid.
        for (const auto address : {0x17ffff000ULL, 0x17ffff001ULL, 0x180000000ULL}) {
            Guest guest(directory);
            guest.write_page(address);
            guest.append(marker(address));

            if (address == 0x17ffff000ULL) {
                check(guest.poll(passion_end) == std::vector<Ready>{{address, 0, 0}},
                      "final Passion page");
            } else {
                rejects(
                        [&] {
                            guest.poll(passion_end);
                        },
                        "beyond the Passion mailbox");
            }
        }

        // Free keeps its 8 GiB range, beyond the Passion end.
        {
            Guest guest(directory);
            guest.write_page(passion_end);
            guest.append(marker(passion_end));
            check(guest.poll() == std::vector<Ready>{{passion_end, 0, 0}}, "Free beyond Passion");
        }
        {
            Guest guest(directory);
            guest.write_page(page, 2);
            guest.append(marker(page));
            rejects(
                    [&] {
                        guest.poll();
                    },
                    "page version");
        }
        {
            Guest guest(directory);

            for (unsigned i = 0; i < 9; ++i) {
                const auto address = page + std::uint64_t{i} * 4096;
                guest.write_page(address);
                guest.append(marker(address));
            }

            rejects(
                    [&] {
                        guest.poll();
                    },
                    "more than eight unbound producers");
        }
        {
            Guest guest(directory);
            guest.write_page(0x68d65000ULL);
            guest.write_page(0x68db8000ULL);
            guest.append(slog2() + "QG3 READY paddr=0x68d65000 length=4096\r\n");
            guest.append(
                    "Jan 01 00:00:34.726        ivi_compositor.16515084               stderr      0  QG3 READY paddr=0x68db8000 length=4096\r\n");
            check(guest.poll() == std::vector<Ready>{{0x68d65000ULL, 0, 0}, {0x68db8000ULL, 0, 0}},
                  "slog2 compositor incarnations");
        }

        for (const auto *tail :
             {"QG3 READY paddr=0xZZZ length=4096\n", "QG3 READY paddr=0x40002000 length=8192\n",
              "QG3 READY paddr=0x240002000 length=4096\n"}) {
            Guest guest(directory);
            guest.append(slog2() + tail);
            rejects(
                    [&] {
                        guest.poll();
                    },
                    "malformed slog2 marker");
        }

        {
            Guest guest(directory);
            guest.append(
                    "Jan 01 00:00:31.516     ivi_compositor.16318491..0              default*  9000  QQnxiIntegration: failed to obtain EGL display: 3002\n"
                    "Jan 01 00:00:32.000        ivi_compositor.16318491               default*  9000  QG4 silenced\n" +
                    slog2() + "QG3 READY paddr=0x40002000 " + std::string(300, 'x') + "\n" +
                    slog2() + std::string(300, 'y') + "\n");
            check(guest.poll().empty(), "ordinary and overlong slog2 lines ignored");
            guest.append(slog2() + "QG3 READY paddr=0x40002000 length=4096\r\n");
            check(guest.poll() == std::vector<Ready>{{page, 0, 0}}, "marker after overlong lines");
        }

        std::filesystem::remove_all(directory);
        std::cout << "QNX serial READY: framing, owners, bounds, identity, slog2 PASS\n";

        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';

        return 1;
    }
}
