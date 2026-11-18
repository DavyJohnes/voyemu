#pragma once
#include "frame_transport.hpp"
#include "model.hpp"
#include <algorithm>
#include <array>
#include <stdexcept>
namespace voyemu {
inline std::filesystem::path qnx_capture_project(const std::filesystem::path &session,
                                                 const std::filesystem::path &output) {
    if (!session.is_absolute() || session.lexically_normal() != session || !output.is_absolute() ||
        output.lexically_normal() != output) {
        throw std::runtime_error("QNX_CAPTURE_PATH: absolute normalized paths required");
    }

    auto parent = session.parent_path();
    const auto token = session.filename().string();

    if (token.size() != 32 || token.find_first_not_of("0123456789abcdef") != std::string::npos ||
        parent.filename() != "sessions" ||
        parent.parent_path().filename() != "free-09-chinese-20260106-6.0.3" ||
        parent.parent_path().parent_path().filename() != "machines" ||
        parent.parent_path().parent_path().parent_path().filename() != ".local") {
        throw std::runtime_error("QNX_CAPTURE_PATH: owned Free A9 session required");
    }

    auto root = parent.parent_path().parent_path().parent_path().parent_path();
    const auto relative = output.lexically_relative(root / ".local/build/tmp");

    if (relative.empty() || relative == "." || *relative.begin() == "..") {
        throw std::runtime_error("QNX_CAPTURE_PATH: output must be project local scratch");
    }

    return root;
}

inline void qnx_capture_journal(const std::filesystem::path &root,
                                const std::filesystem::path &journal) {
    if (!journal.is_absolute() || journal.lexically_normal() != journal) {
        throw std::runtime_error("QNX_CAPTURE_PATH: normalized absolute journal required");
    }

    const auto relative = journal.lexically_relative(
            root / ".local/machines/free-09-chinese-20260106-6.0.3/checkpoints");
    std::vector<std::filesystem::path> parts(relative.begin(), relative.end());

    if (parts.size() != 2 || parts[1] != "gpu" || parts[0].string().empty() ||
        parts[0].string().find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789-") !=
                std::string::npos) {
        throw std::runtime_error("QNX_CAPTURE_PATH: dedicated Free A9 checkpoint journal required");
    }
}

inline Json qnx_capture_initial_state(const Json &profile) {
    const auto state = select_vehicle_startup(VehicleStartup::fresh, profile.at("initial_state"),
                                              nullptr);

    if (state.at("raw_oem").at("speed") != 0 || state.at("raw_oem").at("gear") != 0) {
        throw std::runtime_error("QNX_CAPTURE_SEED: neutral profile defaults required");
    }

    return state;
}

// First-profile scene geometry only, never an OCR or visual correctness oracle.
inline std::array<double, 3> qnx_capture_scene_geometry(const frames::Frame &frame) {
    if (frame.width != 1920 || frame.height != 720 || frame.stride != 7680 ||
        frame.bgra.size() != frames::pixel_bytes) {
        throw std::runtime_error("QNX_CAPTURE_FRAME: original geometry required");
    }

    const std::array<std::array<unsigned, 4>, 3> regions{
            {{100, 190, 700, 650}, {1250, 190, 1820, 650}, {20, 660, 1850, 710}}};
    std::array<double, 3> result{};

    for (unsigned i = 0; i < regions.size(); ++i) {
        unsigned lit = 0, total = 0;
        const auto &a = regions[i];

        for (unsigned y = a[1]; y < a[3]; y += 4) {
            for (unsigned x = a[0]; x < a[2]; x += 4) {
                const auto p = (y * frame.width + x) * 4;
                ++total;

                if (std::max({std::to_integer<unsigned>(frame.bgra[p]),
                              std::to_integer<unsigned>(frame.bgra[p + 1]),
                              std::to_integer<unsigned>(frame.bgra[p + 2])}) > 35) {
                    ++lit;
                }
            }
        }

        result[i] = double(lit) / total;
    }

    return result;
}
} // namespace voyemu
