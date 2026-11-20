#pragma once
#include "error.hpp"
#include "os/fs.hpp"
#include "passion_watch_stream.hpp"
#include "process.hpp"
#include <fstream>
namespace voyemu::media {
// Called by Session after its retained SDK/ADB fence; never exported to the frontend.
inline PassionWatchStream::Child
passion_owned_watch(ProcessSpec spec, const std::filesystem::path &private_watch_directory) {
    if (spec.detached || !spec.output.is_absolute() ||
        spec.output.parent_path() != private_watch_directory ||
        spec.output != spec.output.lexically_normal()) {
        throw VoyemuError("OEM_DISPLAY_POWER", "private retained output required");
    }

    struct Owned {
        ChildProcess process;
        std::filesystem::path output, directory;
        std::size_t offset{};
        bool preserve{};
    };
    auto owned = std::make_shared<Owned>();
    owned->output = spec.output;
    owned->directory = private_watch_directory;
    owned->process = ChildProcess::launch(spec);
    auto regular = [](const std::filesystem::path &path) {
        auto status = std::filesystem::symlink_status(path);

        if (!std::filesystem::is_regular_file(status) || std::filesystem::is_symlink(status)) {
            throw VoyemuError("OEM_DISPLAY_POWER", "owned output is not regular");
        }
    };

    return {[owned] {
                return owned->process.alive();
            },
            [owned, regular](std::size_t maximum) {
                if (!std::filesystem::exists(owned->output)) {
                    return std::string{};
                }

                regular(owned->output);
                const auto size = std::filesystem::file_size(owned->output);

                if (size > 65536 || size < owned->offset || size - owned->offset > maximum) {
                    throw VoyemuError("OEM_DISPLAY_POWER", "bounded watch output changed");
                }

                std::ifstream stream(owned->output, std::ios::binary);

                if (!stream) {
                    throw VoyemuError("OEM_DISPLAY_POWER", "watch read unavailable");
                }

                stream.seekg(static_cast<std::streamoff>(owned->offset));
                std::string bytes(size - owned->offset, '\0');
                stream.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));

                if (stream.gcount() != static_cast<std::streamsize>(bytes.size())) {
                    throw VoyemuError("OEM_DISPLAY_POWER", "watch output shortened");
                }

                owned->offset += bytes.size();

                return bytes;
            },
            [owned, regular] {
                owned->process.terminate(250);

                if (owned->process.alive()) {
                    throw VoyemuError("OEM_DISPLAY_POWER", "retained watch cleanup unverified");
                }

                if (!std::filesystem::exists(owned->output)) {
                    return;
                }

                regular(owned->output);
                auto original_bytes = std::filesystem::file_size(owned->output);

                if (original_bytes > 65536) {
                    owned->preserve = true;
                    os::resize_file(owned->output, 65536);
                }

                auto bytes = std::filesystem::file_size(owned->output);
                Json receipt = {{"schema", 1},
                                {"source", "retained owned display-power watch child"},
                                {"bytes", bytes},
                                {"original_bytes", original_bytes},
                                {"sha256_scope",
                                 original_bytes > bytes ? "retained prefix" : "complete output"},
                                {"read_bytes", owned->offset},
                                {"sha256", sha256_file(owned->output)},
                                {"pid", owned->process.pid()},
                                {"exit_code", owned->process.exit_code()},
                                {"terminal", true},
                                {"preserved_failure", owned->preserve}};
                write_private_json(owned->directory / "last-stop.json", receipt);

                if (owned->preserve) {
                    const auto failure = owned->directory / "last-failed-watch.jsonl";

                    if (std::filesystem::exists(failure)) {
                        regular(failure);
                        std::filesystem::remove(failure);
                    }

                    std::filesystem::rename(owned->output, failure);
                    write_private_json(owned->directory / "last-failure.json", receipt);
                } else {
                    std::filesystem::remove(owned->output);
                }
            },
            [owned] {
                owned->preserve = true;
            }};
}
} // namespace voyemu::media
