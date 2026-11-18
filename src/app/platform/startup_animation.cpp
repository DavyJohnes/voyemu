#include "startup_animation.hpp"
#include "check.hpp"
#include "error.hpp"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <map>
#include <sstream>
#include <stdexcept>

namespace voyemu::window {
namespace {
constexpr std::uint64_t maximum_file = 256ULL << 20, maximum_entry = 32ULL << 20;

constexpr Check need{"WINDOW_STARTUP"};

std::uint32_t little(const std::string &bytes, std::size_t at, unsigned width) {
    need(at + width <= bytes.size(), "truncated ZIP record");
    std::uint32_t value = 0;

    for (unsigned i = 0; i < width; ++i) {
        value |= std::uint32_t(static_cast<unsigned char>(bytes[at + i])) << (8 * i);
    }

    return value;
}

std::string read_at(std::ifstream &source, std::uint64_t offset, std::uint64_t bytes) {
    std::string data(bytes, '\0');

    source.seekg(static_cast<std::streamoff>(offset));
    need(bool(source.read(data.data(), static_cast<std::streamsize>(bytes))), "unreadable ZIP");

    return data;
}
} // namespace

StartupAnimation read_startup_animation(const std::filesystem::path &file) {
    StartupAnimation animation;

    animation.file = file;
    const auto size = std::filesystem::file_size(file);
    need(size >= 22 && size <= maximum_file, "bounded ZIP required");
    std::ifstream source(file, std::ios::binary);
    need(bool(source), "unreadable ZIP");
    const auto tail_bytes = std::min<std::uint64_t>(size, 22 + 0xffff);
    const auto tail = read_at(source, size - tail_bytes, tail_bytes);
    auto end = std::string::npos;

    for (auto at = tail.size() - 22 + 1; at-- > 0;) {
        if (little(tail, at, 4) == 0x06054b50) {
            end = at;
            break;
        }
    }

    need(end != std::string::npos, "ZIP end record missing");
    const auto count = little(tail, end + 10, 2), directory_bytes = little(tail, end + 12, 4),
               directory_offset = little(tail, end + 16, 4);
    need(little(tail, end + 4, 4) == 0 && count <= 4096 &&
                 std::uint64_t(directory_offset) + directory_bytes <= size,
         "single-disk bounded ZIP required");
    const auto directory = read_at(source, directory_offset, directory_bytes);
    struct Entry {
        std::uint64_t offset, bytes;
        bool stored;
    };
    std::map<std::string, Entry> entries;
    std::size_t at = 0;

    for (unsigned i = 0; i < count; ++i) {
        need(little(directory, at, 4) == 0x02014b50, "ZIP directory entry expected");
        const auto method = little(directory, at + 10, 2),
                   compressed = little(directory, at + 20, 4),
                   bytes = little(directory, at + 24, 4),
                   name_bytes = little(directory, at + 28, 2),
                   extra = little(directory, at + 30, 2), comment = little(directory, at + 32, 2),
                   local = little(directory, at + 42, 4);
        need(at + 46 + name_bytes <= directory.size(), "truncated ZIP name");
        const auto name = directory.substr(at + 46, name_bytes);
        at += 46 + name_bytes + extra + comment;

        if (name.ends_with('/')) {
            continue;
        }

        const auto header = read_at(source, local, 30);
        need(little(header, 0, 4) == 0x04034b50, "ZIP local header expected");
        const std::uint64_t data = std::uint64_t(local) + 30 + little(header, 26, 2) +
                                   little(header, 28, 2);
        need(data + compressed <= size, "ZIP entry exceeds archive");
        need(entries.emplace(name, Entry{data, bytes, method == 0 && compressed == bytes}).second,
             "duplicate ZIP entry");
    }

    const auto desc = entries.find("desc.txt");
    need(desc != entries.end() && desc->second.stored && desc->second.bytes <= 4096,
         "stored desc.txt required");
    std::istringstream lines(read_at(source, desc->second.offset, desc->second.bytes));
    std::string line;
    bool header = false;

    while (std::getline(lines, line)) {
        std::istringstream fields(line);

        if (!header) {
            if (!(fields >> animation.width)) {
                continue;
            }

            need(bool(fields >> animation.height >> animation.fps) && animation.width >= 1 &&
                         animation.width <= 3840 && animation.height >= 1 &&
                         animation.height <= 2160 && animation.fps >= 1 && animation.fps <= 60,
                 "animation up to 3840x2160 at 1..60 fps required");
            header = true;
            continue;
        }

        std::string type, path;
        unsigned repeat = 0, pause = 0;

        if (!(fields >> type)) {
            continue;
        }

        need((type == "p" || type == "c") && bool(fields >> repeat >> pause >> path) &&
                     !path.empty() && path.find('/') == std::string::npos && repeat <= 1000 &&
                     pause <= 10000,
             "desc.txt part line invalid");
        std::vector<StartupAnimation::Frame> frames;

        for (auto it = entries.lower_bound(path + "/");
             it != entries.end() && it->first.starts_with(path + "/"); ++it) {
            const auto member = it->first.substr(path.size() + 1);

            if (member.find('/') != std::string::npos || !member.ends_with(".png")) {
                continue;
            }

            need(it->second.stored && it->second.bytes > 0 && it->second.bytes <= maximum_entry,
                 "stored bounded PNG frame required");
            frames.push_back({it->second.offset, it->second.bytes});
        }

        const auto part = animation.parts.size();
        const auto passes = std::max(repeat, 1u);
        const auto added = frames.empty() ? 0 : passes * (frames.size() + pause);
        need(part < 64 && added <= 100000 - animation.timeline.size(),
             "bounded animation required");
        animation.parts.push_back(frames);

        for (unsigned pass = 0; pass < passes && !frames.empty(); ++pass) {
            for (std::size_t frame = 0; frame < frames.size(); ++frame) {
                animation.timeline.emplace_back(part, frame);
            }

            for (unsigned held = 0; held < pause; ++held) {
                animation.timeline.push_back(animation.timeline.back());
            }
        }
    }

    need(header && !animation.timeline.empty(), "animation without frames");

    return animation;
}

std::pair<std::size_t, std::size_t> startup_frame(const StartupAnimation &animation,
                                                  double seconds) {
    const auto last = animation.timeline.size() - 1;
    const double index = std::isfinite(seconds) && seconds > 0 ? seconds * animation.fps : 0;

    return animation.timeline.at(index >= double(last) ? last : static_cast<std::size_t>(index));
}

std::string read_startup_frame(const StartupAnimation &animation,
                               std::pair<std::size_t, std::size_t> frame) {
    const auto &entry = animation.parts.at(frame.first).at(frame.second);
    std::ifstream source(animation.file, std::ios::binary);

    need(bool(source), "unreadable ZIP");

    return read_at(source, entry.offset, entry.bytes);
}
} // namespace voyemu::window
