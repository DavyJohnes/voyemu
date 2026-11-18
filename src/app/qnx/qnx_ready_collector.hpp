#pragma once
#include "error.hpp"
#ifndef _WIN32
#include <algorithm>
#include <array>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <fcntl.h>
#include <filesystem>
#include <mutex>
#include <regex>
#include <stdexcept>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>
#include <vector>

namespace voyemu {
// Only the bounded owned RAM console ring and advertised eight-byte page identities are read.
// No Session callbacks run on this thread; the normal tick owns worker creation.
class QnxReadyCollector {
public:
    struct Ready {
        std::uint64_t address = 0, pid = 0, incarnation = 0;
        bool operator==(const Ready &) const = default;
    };

    QnxReadyCollector(const std::filesystem::path &ram, std::uint64_t address, std::size_t extent,
                      std::uint64_t mailbox_end = 0x240000000ULL)
        : path_(ram), address_(address), mailbox_end_(mailbox_end), extent_(extent) {
        if ((mailbox_end != 0x180000000ULL && mailbox_end != 0x240000000ULL) || address < base ||
            extent < 32 || extent > 0x40000 || address > mailbox_end - extent) {
            throw VoyemuError("QNX_READY", "invalid bounded ring");
        }

        fd_ = ::open(path_.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW);

        if (fd_ < 0) {
            throw VoyemuError("QNX_READY", "owned RAM reader open failed");
        }

        if (::fstat(fd_, &identity_) || !ordinary(identity_) || !same_path()) {
            ::close(fd_);
            fd_ = -1;
            throw VoyemuError("QNX_READY", "ordinary owned RAM required");
        }

        try {
            thread_ = std::thread([this] {
                run();
            });
        } catch (...) {
            ::close(fd_);
            fd_ = -1;
            throw;
        }
    }

    ~QnxReadyCollector() {
        stop();

        if (fd_ >= 0) {
            ::close(fd_);
        }
    }

    QnxReadyCollector(const QnxReadyCollector &) = delete;

    void stop() {
        {
            std::lock_guard lock(mutex_);
            stopping_ = true;
        }
        wake_.notify_all();

        if (thread_.joinable()) {
            thread_.join();
        }
    }

    std::vector<Ready> ready() {
        if (!same_path()) {
            throw VoyemuError("QNX_READY", "RAM incarnation changed");
        }

        std::lock_guard lock(mutex_);

        if (!error_.empty()) {
            throw std::runtime_error(error_);
        }

        return ready_;
    }

    std::vector<std::uint64_t> addresses() {
        std::vector<std::uint64_t> result;

        for (const auto &value : ready()) {
            result.push_back(value.address);
        }

        return result;
    }

private:
    static constexpr std::uint64_t base = 0x40000000;

    static bool ordinary(const struct stat &value) {
        return S_ISREG(value.st_mode) && value.st_uid == ::geteuid() && value.st_nlink == 1;
    }

    bool same_path() {
        struct stat value{};

        return !::lstat(path_.c_str(), &value) && ordinary(value) &&
               value.st_dev == identity_.st_dev && value.st_ino == identity_.st_ino;
    }

    bool read(std::uint64_t address, void *output, std::size_t bytes) {
        return ::pread(fd_, output, bytes, static_cast<off_t>(address - base)) ==
               static_cast<ssize_t>(bytes);
    }

    void accept(std::uint64_t address, std::uint64_t pid = 0, std::uint64_t incarnation = 0) {
        if (address < base || address > mailbox_end_ - 4096 || address % 4096) {
            throw VoyemuError("QNX_READY", "invalid advertised address");
        }

        if (!incarnation) {
            std::lock_guard lock(mutex_);

            if (std::any_of(ready_.begin(), ready_.end(), [&](const auto &item) {
                    return item.address == address;
                })) {
                return;
            }
        }

        std::array<std::uint32_t, 2> page{};

        if (!read(address, page.data(), sizeof(page)) || page[0] != 0x58464751 || page[1] != 3) {
            if (incarnation) {
                return;
            }

            throw VoyemuError("QNX_READY", "advertised page identity");
        }

        std::array<std::uint64_t, 2> identity{};

        if (incarnation && (!read(address + 240, identity.data(), sizeof(identity)) ||
                            identity[0] != pid || identity[1] != incarnation)) {
            return; // Retained UART data may describe a retired page.
        }

        if (!same_path()) {
            throw VoyemuError("QNX_READY", "source incarnation changed");
        }

        std::lock_guard lock(mutex_);
        const Ready value{address, pid, incarnation};
        auto found = std::find_if(ready_.begin(), ready_.end(), [&](const auto &item) {
            return item.address == address;
        });

        if (found != ready_.end()) {
            if (incarnation) {
                *found = value;
            }

            return;
        }

        if (!incarnation && ready_.size() >= 8) {
            throw VoyemuError("QNX_READY", "more than eight bounded workers");
        }

        ready_.push_back(value);
    }

    void sample() {
        if (!same_path()) {
            throw VoyemuError("QNX_READY", "RAM incarnation changed");
        }

        std::vector<char> bytes(extent_);

        if (!read(address_, bytes.data(), bytes.size())) {
            return;
        }

        auto word = [&](std::size_t offset) {
            std::uint32_t result = 0;

            for (unsigned i = 0; i < 4; ++i) {
                result |= std::uint32_t(static_cast<unsigned char>(bytes[offset + i])) << (8 * i);
            }

            return result;
        };

        if (word(0) != 0xbeefeeee) {
            return;
        }

        const auto capacity = word(4), cursor = word(8);

        if (!capacity || capacity > bytes.size() - 32 || cursor >= capacity) {
            throw VoyemuError("QNX_READY", "corrupt ring header");
        }

        std::string text(bytes.data() + 32 + cursor, bytes.data() + 32 + capacity);
        text.append(bytes.data() + 32, bytes.data() + 32 + cursor);
        static const std::regex pattern(
                R"(QG3 READY paddr=(0x[0-9a-f]+) length=4096(?: pid=([1-9][0-9]*) incarnation=(0x[0-9a-f]+))?)");

        for (auto it = std::sregex_iterator(text.begin(), text.end(), pattern);
             it != std::sregex_iterator(); ++it) {
            const auto &match = *it;
            const auto pid = match[2].matched ? std::stoull(match[2].str()) : 0,
                       incarnation = match[3].matched ? std::stoull(match[3].str(), nullptr, 16)
                                                      : 0;

            if (match[3].matched && !incarnation) {
                throw VoyemuError("QNX_READY", "zero incarnation");
            }

            accept(std::stoull(match[1].str(), nullptr, 16), pid, incarnation);
        }
    }

    void run() noexcept {
        try {
            for (;;) {
                {
                    std::lock_guard lock(mutex_);

                    if (stopping_) {
                        return;
                    }
                }
                sample();
                std::unique_lock lock(mutex_);

                if (wake_.wait_for(lock, std::chrono::milliseconds(20), [&] {
                        return stopping_;
                    })) {
                    return;
                }
            }
        } catch (const std::exception &error) {
            std::lock_guard lock(mutex_);
            error_ = error.what();
        } catch (...) {
            std::lock_guard lock(mutex_);
            error_ = "QNX_READY: collector failed";
        }
    }

    std::filesystem::path path_;
    std::uint64_t address_, mailbox_end_;
    std::size_t extent_;
    int fd_ = -1;
    struct stat identity_{};
    std::mutex mutex_;
    std::condition_variable wake_;
    bool stopping_ = false;
    std::string error_;
    std::vector<Ready> ready_;
    std::thread thread_;
};
} // namespace voyemu
#endif
