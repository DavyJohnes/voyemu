#pragma once
// Archive fixtures for tests: stored-method ZIP and gzip TAR files written without a host archiver,
// in ustar headers and stored deflate blocks.
#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

struct ArchiveFixtureEntry {
    std::string name, data;
    bool link = false, bad_crc = false;
};

inline std::uint32_t archive_fixture_crc32(const std::string &data) {
    std::uint32_t crc = 0xffffffffu;

    for (unsigned char byte : data) {
        crc ^= byte;

        for (unsigned bit = 0; bit < 8; ++bit) {
            crc = (crc & 1) ? (crc >> 1) ^ 0xedb88320u : (crc >> 1);
        }
    }

    return ~crc;
}

inline std::string stored_zip_fixture(const std::vector<ArchiveFixtureEntry> &entries) {
    auto little = [](std::uint64_t value, unsigned width) {
        std::string out(width, '\0');

        for (unsigned i = 0; i < width; ++i) {
            out[i] = static_cast<char>(value >> (8 * i));
        }

        return out;
    };
    std::string body, central;
    std::uint16_t count = 0;

    for (const auto &item : entries) {
        const std::string payload = item.link ? std::string() : item.data;
        const auto crc = item.bad_crc ? archive_fixture_crc32(payload) ^ 1u
                                      : archive_fixture_crc32(payload);
        const std::uint64_t offset = body.size();
        const auto mode = item.link ? 0120777u : 0100644u;
        body += little(0x04034b50, 4) + little(20, 2) + little(0, 2) + little(0, 2) + little(0, 2) +
                little(0, 2) + little(crc, 4) + little(payload.size(), 4) +
                little(payload.size(), 4) + little(item.name.size(), 2) + little(0, 2) + item.name +
                payload;
        central += little(0x02014b50, 4) + little(0x031e, 2) + little(20, 2) + little(0, 2) +
                   little(0, 2) + little(0, 2) + little(0, 2) + little(crc, 4) +
                   little(payload.size(), 4) + little(payload.size(), 4) +
                   little(item.name.size(), 2) + little(0, 2) + little(0, 2) + little(0, 2) +
                   little(0, 2) + little(std::uint64_t(mode) << 16, 4) + little(offset, 4) +
                   item.name;
        ++count;
    }

    const auto central_offset = body.size();
    body += central;
    body += little(0x06054b50, 4) + little(0, 2) + little(0, 2) + little(count, 2) +
            little(count, 2) + little(central.size(), 4) + little(central_offset, 4) + little(0, 2);

    return body;
}

inline std::string gzip_tar_fixture(const std::vector<ArchiveFixtureEntry> &entries) {
    std::string tar;
    auto octal = [](std::string &header, unsigned at, unsigned width, std::uint64_t value) {
        for (unsigned i = width - 1; i-- > 0;) {
            header[at + i] = static_cast<char>('0' + (value & 7));
            value >>= 3;
        }
    };

    for (const auto &item : entries) {
        std::string header(512, '\0');
        const std::string payload = item.link ? std::string() : item.data;

        if (item.name.size() >= 100) {
            throw std::runtime_error("fixture name");
        }

        header.replace(0, item.name.size(), item.name);
        octal(header, 100, 8, 0644);
        octal(header, 108, 8, 0);
        octal(header, 116, 8, 0);
        octal(header, 124, 12, payload.size());
        octal(header, 136, 12, 0);
        header[156] = item.link ? '2' : '0';

        if (item.link) {
            header.replace(157, 7, "outside");
        }

        header.replace(257, 6, std::string("ustar\0", 6));
        header.replace(263, 2, "00");
        header.replace(148, 8, "        ");
        unsigned sum = 0;

        for (unsigned char byte : header) {
            sum += byte;
        }

        octal(header, 148, 7, sum);
        header[154] = '\0';
        header[155] = ' ';
        tar += header + payload + std::string((512 - payload.size() % 512) % 512, '\0');
    }

    tar += std::string(1024, '\0');
    auto little = [](std::uint64_t value, unsigned width) {
        std::string out(width, '\0');

        for (unsigned i = 0; i < width; ++i) {
            out[i] = static_cast<char>(value >> (8 * i));
        }

        return out;
    };
    std::string gzip("\x1f\x8b\x08\x00\x00\x00\x00\x00\x00\xff", 10);

    for (std::size_t at = 0; at < tar.size(); at += 65535) {
        const auto size = std::min<std::size_t>(65535, tar.size() - at);
        gzip += std::string(1, at + size == tar.size() ? '\x01' : '\x00') + little(size, 2) +
                little(~size & 0xffff, 2) + tar.substr(at, size);
    }

    return gzip + little(archive_fixture_crc32(tar), 4) + little(tar.size() & 0xffffffff, 4);
}

inline void write_archive_fixture(const std::filesystem::path &file,
                                  const std::vector<ArchiveFixtureEntry> &entries, bool zip) {
    const auto data = zip ? stored_zip_fixture(entries) : gzip_tar_fixture(entries);
    std::ofstream output(file, std::ios::binary);

    output.write(data.data(), static_cast<std::streamsize>(data.size()));
    output.close();

    if (!output) {
        throw std::runtime_error("fixture write");
    }
}
