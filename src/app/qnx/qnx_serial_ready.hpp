#pragma once
#include "error.hpp"
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <regex>
#include <span>
#include <sstream>
#include <string>
#include <utility>
#include <vector>
namespace voyemu {
// Frame producer pages that the QNX guest announced with QG3 READY in its session UART lines
// (QnxSerialLines). Each line is read once, so an accepted page stays until the generation ends;
// the caller still checks its owner before starting a worker.
class QnxSerialReady {
public:
    struct Ready {
        std::uint64_t address = 0, pid = 0, incarnation = 0;
        bool operator==(const Ready &) const = default;
    };

    // Reads guest physical memory and returns the number of bytes read.
    using Read = std::function<std::size_t(std::uint64_t, std::span<std::byte>)>;

    void add(const std::string &lines) {
        std::istringstream input(lines);

        for (std::string line; std::getline(input, line);) {
            if (line.starts_with("QG3 READY ")) {
                pending_.push_back(std::move(line));
            }
        }
    }

    // Accepts the added markers in UART order against the guest RAM below mailbox_end.
    const std::vector<Ready> &ready(const Read &read, std::uint64_t mailbox_end) {
        static const std::regex marker(
                R"(^QG3 READY paddr=(0x[0-9a-f]+) length=4096(?: pid=([1-9][0-9]*) incarnation=(0x[0-9a-f]+))?$)");
        const auto lines = std::exchange(pending_, {});

        for (const auto &line : lines) {
            std::smatch match;

            if (!std::regex_match(line, match, marker)) {
                throw VoyemuError("QNX_READY", "malformed UART marker");
            }

            const auto pid = match[2].matched ? std::stoull(match[2].str()) : 0,
                       incarnation = match[3].matched ? std::stoull(match[3].str(), nullptr, 16)
                                                      : 0;

            if (match[3].matched && !incarnation) {
                throw VoyemuError("QNX_READY", "zero incarnation");
            }

            accept(read, mailbox_end, std::stoull(match[1].str(), nullptr, 16), pid, incarnation);
        }

        return ready_;
    }

private:
    static constexpr std::uint64_t base = 0x40000000;

    void accept(const Read &read, std::uint64_t mailbox_end, std::uint64_t address,
                std::uint64_t pid, std::uint64_t incarnation) {
        if (address < base || address > mailbox_end - 4096 || address % 4096) {
            throw VoyemuError("QNX_READY", "invalid advertised address");
        }

        auto found = std::find_if(ready_.begin(), ready_.end(), [&](const auto &item) {
            return item.address == address;
        });

        if (!incarnation && found != ready_.end()) {
            return;
        }

        std::array<std::uint32_t, 2> page{};

        if (read(address, std::as_writable_bytes(std::span(page))) != sizeof(page) ||
            page[0] != 0x58464751 || page[1] != 3) {
            if (incarnation) {
                return;
            }

            throw VoyemuError("QNX_READY", "advertised page identity");
        }

        std::array<std::uint64_t, 2> owner{};

        if (incarnation &&
            (read(address + 240, std::as_writable_bytes(std::span(owner))) != sizeof(owner) ||
             owner[0] != pid || owner[1] != incarnation)) {
            return; // An earlier UART line may describe a retired page.
        }

        if (found != ready_.end()) {
            *found = {address, pid, incarnation};

            return;
        }

        if (!incarnation && ready_.size() >= 8) {
            throw VoyemuError("QNX_READY", "more than eight bounded workers");
        }

        ready_.push_back({address, pid, incarnation});
    }

    std::vector<std::string> pending_;
    std::vector<Ready> ready_;
};
} // namespace voyemu
