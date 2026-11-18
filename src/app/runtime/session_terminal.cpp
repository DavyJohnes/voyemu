#include "error.hpp"
#include "session_internal.hpp"

namespace voyemu {
using namespace session_detail;
namespace session_detail {
void terminal_path_guard(const std::filesystem::path &path) {
    auto absolute = std::filesystem::absolute(path).lexically_normal();

    for (const auto &current : os::ancestors(absolute)) {
        if (std::filesystem::is_symlink(std::filesystem::symlink_status(current))) {
            throw VoyemuError("QNX_CLEANUP_PATH", "symlink rejected");
        }

        if (os::reparse_point(current)) {
            throw VoyemuError("QNX_CLEANUP_PATH", "reparse point rejected");
        }
    }
}

std::filesystem::path qnx_project(const std::filesystem::path &directory,
                                  const std::string &session_id) {
    if (!hex_text(session_id, 32)) {
        throw VoyemuError("QNX_CLEANUP_OWNER", "invalid session id");
    }

    auto path = std::filesystem::absolute(directory).lexically_normal();
    terminal_path_guard(path);
    auto sessions = path.parent_path();
    auto machine = sessions.parent_path();
    auto machines = machine.parent_path();
    auto local = machines.parent_path();

    if (path.filename() != session_id || machine.filename().string().empty() ||
        sessions.filename() != "sessions" || machines.filename() != "machines" ||
        local.filename() != ".local") {
        throw VoyemuError("QNX_CLEANUP_OWNER", "isolated native session ancestry required");
    }

    return local.parent_path();
}

void exclusive_handoff(const std::filesystem::path &path, const std::string &text) {
    terminal_path_guard(path);
    ensure_private_directory(path.parent_path());
    write_handoff_file(path, text);
    std::ifstream input(path, std::ios::binary);
    std::string observed{std::istreambuf_iterator<char>(input), {}};

    if (observed != text) {
        throw VoyemuError("QNX_HANDOFF", "readback failed; RAM retained");
    }
}

Json terminal_frame_evidence(const std::filesystem::path &directory) {
    auto path = directory / "frame.bgra";

    if (!std::filesystem::exists(path) && !std::filesystem::is_symlink(path)) {
        return {{"status", "no_exported_frame"},
                {"frame_count", 0},
                {"pixel_reaction_verified", false}};
    }

    terminal_path_guard(path);

    if (!std::filesystem::is_regular_file(path)) {
        throw VoyemuError("QNX_FRAME_RECORD", "regular backing required");
    }

    constexpr std::uint64_t pixel_bytes = 1920ULL * 720 * 4, slot_bytes = 64 + pixel_bytes,
                            total_bytes = 128 + 3 * slot_bytes;

    if (std::filesystem::file_size(path) != total_bytes) {
        return {{"status", "invalid_frame_extent"}, {"pixel_reaction_verified", false}};
    }

    std::array<unsigned char, 128> header{};
    std::ifstream input(path, std::ios::binary);
    input.read(reinterpret_cast<char *>(header.data()), header.size());

    if (input.gcount() != 128) {
        throw VoyemuError("QNX_FRAME_RECORD", "header read failed");
    }

    auto word = [&](unsigned at, unsigned count) {
        std::uint64_t value = 0;

        for (unsigned i = 0; i < count; ++i) {
            value |= std::uint64_t(header[at + i]) << (8 * i);
        }

        return value;
    };

    if (word(0, 8) != 0x3152474246585156ULL || word(8, 4) != 1 || word(12, 4) != 128 ||
        word(16, 4) != 1920 || word(20, 4) != 720 || word(24, 4) != 7680 || word(28, 4) != 3 ||
        word(32, 8) != total_bytes || word(40, 8) != slot_bytes) {
        return {{"status", "invalid_frame_header"}, {"pixel_reaction_verified", false}};
    }

    auto sequence = word(64, 8);

    if (!sequence) {
        return {{"status", "initialized_without_export"},
                {"frame_count", 0},
                {"pixel_reaction_verified", false}};
    }

    std::array<unsigned char, 64> slot{};
    input.seekg(128 + (sequence % 3) * slot_bytes);
    input.read(reinterpret_cast<char *>(slot.data()), slot.size());

    if (input.gcount() != 64) {
        throw VoyemuError("QNX_FRAME_RECORD", "slot read failed");
    }

    auto number = [&](unsigned at, unsigned count) {
        std::uint64_t value = 0;

        for (unsigned i = 0; i < count; ++i) {
            value |= std::uint64_t(slot[at + i]) << (8 * i);
        }

        return value;
    };

    if (sequence > UINT64_MAX / 2 || number(0, 8) != sequence * 2 || number(8, 8) != sequence ||
        number(24, 4) != 1920 || number(28, 4) != 720 || number(32, 4) != 7680) {
        return {{"status", "incomplete_final_slot"}, {"pixel_reaction_verified", false}};
    }

    auto owner = read_private_json(directory / "frame-owner.json", 4096);

    if (owner.value("schema", 0) != 1 ||
        owner.value("source", std::string{}) != "original-OEM-Screen-BMP") {
        throw VoyemuError("QNX_FRAME_RECORD", "original Screen owner required");
    }

    return {{"status", "completed_original_Screen_backing"},
            {"frame_count", sequence},
            {"last_sequence", sequence},
            {"producer_address", owner.at("producer_address")},
            {"pixel_reaction_verified", false},
            {"count_scope",
             "monotonic opcode354 publish count; source pixels preserved in frame.bgra"}};
}

Json terminal_native_acks(const std::filesystem::path &directory) {
    Json evidence = Json::object();

    for (const char *name : {"vehicle-ack.json", "wheel-ack.json"}) {
        auto path = directory / name;

        if (!std::filesystem::exists(path)) {
            continue;
        }

        try {
            auto value = read_private_json(path, 4096);

            if (value.value("schema", 0) != 1 || !value.value("guest_acknowledged", false)) {
                throw VoyemuError("QNX_NATIVE_ACK", "invalid native ACK schema");
            }

            evidence[name] = value;
        } catch (const std::exception &error) {
            evidence[name] = {{"status", "invalid_record"}, {"error", error.what()}};
        }
    }

    Json themes = Json::array();

    for (const auto &entry : std::filesystem::directory_iterator(directory)) {
        if (!entry.path().filename().string().starts_with("producer-") || !entry.is_directory() ||
            entry.is_symlink()) {
            continue;
        }

        auto path = entry.path() / "theme-ack.json";

        if (!std::filesystem::exists(path)) {
            continue;
        }

        try {
            themes.push_back(read_private_json(path, 4096));
        } catch (const std::exception &error) {
            themes.push_back({{"status", "invalid_record"}, {"error", error.what()}});
        }
    }

    evidence["theme_producers"] = std::move(themes);
    evidence["scope"] =
            "guest poll acknowledgement only; no application callback or visible pixel reaction is inferred";

    return evidence;
}
} // namespace session_detail

Json finalize_owned_qnx_session(const std::filesystem::path &directory,
                                const std::string &session_id, const std::filesystem::path &ram,
                                const Json &evidence) {
    auto session = std::filesystem::absolute(directory).lexically_normal();
    auto project = qnx_project(session, session_id);
    auto expected = session / "qnx-ram.bin";

    if (std::filesystem::absolute(ram).lexically_normal() != expected) {
        throw VoyemuError("QNX_CLEANUP_PATH", "only exact owned qnx-ram.bin may be removed");
    }

    terminal_path_guard(expected);
    auto owner = read_private_json(session / "session-owner.json", 4096);
    const auto owner_directory = std::filesystem::path(owner.value("directory", std::string{}));

    if (owner.value("schema", 0) != 1 || owner.value("session_id", std::string{}) != session_id ||
        !owner_directory.is_absolute() || owner_directory.lexically_normal() != session ||
        owner.value("origin", std::string{}) != "voyemu-native-supervisor" ||
        !owner.value("ram_absent_before_owned_qemu", false)) {
        throw VoyemuError("QNX_CLEANUP_OWNER", "creation provenance missing");
    }

    if (!evidence.is_object() || !evidence.value("children_terminal", false) ||
        !evidence.contains("qemu") || !evidence["qemu"].is_object() ||
        evidence["qemu"].value("alive", true) ||
        evidence["qemu"].value("pid", std::int64_t{0}) !=
                owner.value("qnx_pid", std::int64_t{-1})) {
        throw VoyemuError("QNX_CLEANUP_LIFETIME", "owned QEMU must be terminal");
    }

    const bool before_launch = evidence.value("qemu_launch_failed_before_child", false) &&
                               evidence["qemu"].value("pid", std::int64_t{-1}) == 0 &&
                               !evidence["qemu"].value("started", true) &&
                               evidence["qemu"].value("exit_code", 0) == -1 &&
                               evidence.value("graphics", Json::array()).empty() &&
                               owner.contains("ram_clone_identity") &&
                               evidence.value("ram_clone_identity", Json{}) ==
                                       owner.at("ram_clone_identity");

    if (!before_launch &&
        (evidence["qemu"].value("pid", std::int64_t{0}) <= 1 ||
         !evidence["qemu"].value("started", true) || evidence["qemu"].value("exit_code", -1) < 0)) {
        throw VoyemuError("QNX_CLEANUP_LIFETIME",
                          "started child exit or exact prelaunch clone evidence required");
    }

    for (const auto &child : evidence.value("graphics", Json::array())) {
        if (child.value("alive", true) || child.value("exit_code", -1) < 0) {
            throw VoyemuError("QNX_CLEANUP_LIFETIME", "owned graphics must be terminal");
        }
    }

    const auto qnx_ram_bytes = qnx_owned_ram_bytes(owner);
    bool exists = std::filesystem::exists(expected);

    if (before_launch &&
        (!exists || cache_file_identity(expected) != owner.at("ram_clone_identity"))) {
        throw VoyemuError("QNX_CLEANUP_RAM", "prelaunch clone identity changed");
    }

    OwnedRamBacking backing(expected, exists, qnx_ram_bytes);
    Json record = evidence;
    record["schema"] = 1;
    record["session_id"] = session_id;
    record["updated_utc"] = utc_now();
    record["ram_path"] = expected.string();
    record["ram_present"] = exists;
    record["frames"] = terminal_frame_evidence(session);
    record["native_acks"] = terminal_native_acks(session);
    record["processed"] = true;
    record["ram_removal_permitted"] = true;
    auto now = std::time(nullptr);
    const auto local = local_time(now);
    char date[16], stamp[32];
    std::strftime(date, sizeof date, "%Y-%m-%d", &local);
    std::strftime(stamp, sizeof stamp, "%Y-%m-%d-%H-%M-%S", &local);
    auto handoff = project / "plans" / date /
                   (std::string(stamp) + "-handoff-qnx-native-" + session_id + "-g" +
                    std::to_string(evidence.value("cluster_generation", std::uint64_t{0})) + ".md");
    std::string text =
            "# Native QNX session terminal handoff\n\nCreated at " + utc_now() +
            " from the native OS clock. Session " + session_id + "; " +
            (before_launch
                     ? "QEMU creation failed before a child existed; only the owned RAM clone is finalized. "
                     : "Only owned QEMU, graphics and UI processes were stopped. ") +
            "Android continuation and wheel-release results are recorded below.\n\nThis records host process termination, native exported frame metadata and guest ACK scopes separately. Guest trace, mount, Screen initialization and visible application reactions are not inferred from an ACK. No guest memory bytes are included. OEM inputs, frame backings, logs and prior handoffs remain unchanged.\n\nThe exact owned RAM backing was validated before this exclusive durable handoff. Removal is permitted only after this record was read back successfully.\n\n```json\n" +
            record.dump(2) + "\n```\n";
    exclusive_handoff(handoff, text);
    auto hash = sha256_file(handoff);

    if (exists) {
        backing.remove();
    }

    return {{"status", exists ? "removed_after_handoff" : "ram_not_created"},
            {"ram_removed", exists},
            {"handoff", handoff.string()},
            {"handoff_sha256", hash},
            {"processed", true}};
}
} // namespace voyemu
