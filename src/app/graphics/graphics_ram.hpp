#pragma once
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <string>

namespace voyemu::graphics {
struct Options;

namespace detail {
class Ram {
public:
    explicit Ram(const Options &);
    ~Ram();
    Ram(const Ram &) = delete;
    Ram &operator=(const Ram &) = delete;

    void check_identity();
    std::span<std::byte> extent(std::uint64_t address, std::uint64_t length);
    void write(std::uint64_t address, std::span<const std::byte> bytes);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    std::span<std::byte> mapped() const;
};

std::string read_control_file(const std::filesystem::path &, std::size_t size);
std::filesystem::path angle_loader_file(const std::filesystem::path &angle);
} // namespace detail
} // namespace voyemu::graphics
