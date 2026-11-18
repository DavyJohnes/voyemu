#include "capture_png.hpp"
#include "frame_transport.hpp"
#include "platform_file.hpp"
#include "qnx_boot_progress.hpp"
#include "session.hpp"
#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <fstream>
#include <iostream>
#include <set>
#include <thread>
#include <vector>
#define NOMINMAX
#include <windows.h>
#include <aclapi.h>
using namespace voyemu;

namespace {
constexpr std::uint64_t probe_disk_floor_bytes = 24ULL * 1024 * 1024 * 1024;

bool probe_disk_reserve_available(std::uint64_t available) {
    return available >= probe_disk_floor_bytes;
}

void probe_disk_reserve_test() {
    if (probe_disk_reserve_available(probe_disk_floor_bytes - 1) ||
        !probe_disk_reserve_available(probe_disk_floor_bytes) ||
        !probe_disk_reserve_available(probe_disk_floor_bytes + 1)) {
        throw std::runtime_error("probe24GiB disk boundary regression");
    }
}

constexpr std::array progress_target_names{"rpcd", "mcd",     "pmonitor", "sleep",    "ksh",
                                           "sh",   "on",      "setting",  "powermgr", "themmgr",
                                           "ivcd", "openssl", "sysmgr"};

unsigned progress_target_priority(std::string_view path) {
    const auto name = path.substr(path.find_last_of(char(47)) + 1);

    return static_cast<unsigned>(
            std::find(progress_target_names.begin(), progress_target_names.end(), name) -
            progress_target_names.begin());
}

void progress_target_priority_test() {
    std::array<std::string_view, 7> paths{
            "proc/boot/bin/ksh", "proc/boot/bin/sleep", "bin/pmonitor", "not-sleep",
            "usr/bin/mcd",       "usr/bin/rpcd",        "mcd/other"};

    std::stable_sort(paths.begin(), paths.end(), [](auto a, auto b) {
        return progress_target_priority(a) < progress_target_priority(b);
    });

    if (paths[0] != "usr/bin/rpcd" || paths[1] != "usr/bin/mcd" || paths[2] != "bin/pmonitor" ||
        paths[3] != "proc/boot/bin/sleep" || paths[4] != "proc/boot/bin/ksh" ||
        progress_target_priority(paths[5]) != progress_target_names.size() ||
        progress_target_priority(paths[6]) != progress_target_names.size() ||
        progress_target_priority("sleep/other") != progress_target_names.size()) {
        throw std::runtime_error("probe MCD/IPC target ordering regression");
    }
}

void capture_probe_frame(const std::filesystem::path &directory,
                         const std::filesystem::path &output, const Json &producers, Json &result,
                         bool progress_due, std::uint64_t observed_ms) {
    const auto owner_path = directory / "frame-owner.json", backing = directory / "frame.bgra";

    if (!std::filesystem::is_regular_file(owner_path) ||
        !std::filesystem::is_regular_file(backing)) {
        return;
    }

    Json owner;
    try {
        owner = read_private_json(owner_path, 4096);
    } catch (const std::exception &) {
        result["frame_capture_skip"] = "owner_unavailable";

        return;
    }

    if (owner.value("schema", 0) != 1 ||
        owner.value("source", std::string{}) != "original-OEM-Screen-BMP" ||
        !owner.contains("producer_address") || !producers.is_array() ||
        std::find(producers.begin(), producers.end(), owner["producer_address"]) ==
                producers.end()) {
        result["frame_capture_skip"] = "owner_not_current_producer";

        return;
    }

    frames::Reader reader(backing);
    auto frame = reader.read_latest();

    if (!frame) {
        return;
    }

    try {
        if (read_private_json(owner_path, 4096) != owner) {
            result["frame_capture_skip"] = "owner_changed";

            return;
        }
    } catch (const std::exception &) {
        result["frame_capture_skip"] = "owner_unavailable";

        return;
    }

    result.erase("frame_capture_skip");
    result["observed_frame_sequence"] = frame->sequence;
    const bool first = !result.contains("first_frame_export");
    const bool latest = progress_due &&
                        (!result.contains("latest_frame_export") ||
                         result["latest_frame_export"].at("sequence") != frame->sequence ||
                         result["latest_frame_export"].at("frame_owner") != owner);

    if (!first && !latest) {
        return;
    }

    const auto bytes = voyemu::output::png_frame(
            std::span(reinterpret_cast<const std::uint8_t *>(frame->bgra.data()),
                      frame->bgra.size()),
            frame->width, frame->height, true);
    result["snapshot_encode_count"] = result.value("snapshot_encode_count", 0u) + 1;
    const auto digest = sha256_bytes(
            std::string_view(reinterpret_cast<const char *>(bytes.data()), bytes.size()));
    auto publish = [&](const char *name, const char *receipt_name, const char *key, bool replace) {
        const auto path = output / name, receipt_path = output / receipt_name;

        if (std::filesystem::is_symlink(path) || std::filesystem::is_symlink(receipt_path)) {
            throw std::runtime_error("probe snapshot symlink rejected");
        }

        if (std::filesystem::exists(path)) {
            if (!replace || !result.contains(key) || !std::filesystem::is_regular_file(path) ||
                sha256_file(path) != result[key].at("png_sha256").get<std::string>()) {
                throw std::runtime_error("probe snapshot changed outside this run");
            }
        } else if (result.contains(key) || std::filesystem::exists(receipt_path)) {
            throw std::runtime_error("probe snapshot receipt mismatch");
        }

        if (replace) {
            const auto pending = output / "cluster.png.pending";
            voyemu::output::write_artifact(pending, bytes);

            if (!MoveFileExW(pending.c_str(), path.c_str(),
                             MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
                const auto error = GetLastError();
                std::filesystem::remove(pending);
                throw std::runtime_error("probe snapshot publish failed: " + std::to_string(error));
            }
        } else {
            voyemu::output::write_artifact(path, bytes);
        }

        Json receipt = {{"schema", 1},
                        {"file", name},
                        {"sequence", frame->sequence},
                        {"timestamp_ns", frame->timestamp_ns},
                        {"observed_ms", observed_ms},
                        {"width", frame->width},
                        {"height", frame->height},
                        {"frame_owner", owner},
                        {"png_sha256", digest}};
        write_private_json(receipt_path, receipt);
        result[key] = std::move(receipt);
        result["actual_exported_frame"] = true;
        result["frame_sequence"] = frame->sequence;
        result["frame_width"] = frame->width;
        result["frame_height"] = frame->height;
        result["frame_owner"] = owner;
    };

    if (first) {
        publish("cluster-first.png", "cluster-first.json", "first_frame_export", false);
    }

    if (latest) {
        publish("cluster.png", "cluster.json", "latest_frame_export", true);
    }
}

struct PostFrameControl {
    bool enabled = false, consumed = false, patch_sent = false, restore_sent = false,
         patch_verified = false;
    std::string nonce, phase = "disabled", failure;
    Json history = Json::array(), baseline = Json::object(), baseline_raw = Json::object(),
         baseline_frame = Json::object(), patch_frame = Json::object(), receipt = Json::object();
    std::uint64_t pid = 0, generation = 0, revision = 0, patch_until = 0, restore_until = 0,
                  patch_sent_ms = 0, restore_sent_ms = 0;
    bool patch_ack_seen = false, restore_ack_seen = false;
    std::uint64_t patch_first_ack_ms = 0, restore_first_ack_ms = 0, patch_ack_frame_sequence = 0,
                  restore_ack_frame_sequence = 0;
};

bool control_terminal(const PostFrameControl &c) {
    return c.enabled && (c.phase == "complete" || c.phase == "failed");
}

bool control_success(const PostFrameControl &c) {
    return !c.enabled || c.phase == "complete";
}

void control_need(bool condition, const char *message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

std::string control_owned_bytes(const std::filesystem::path &path, std::uint64_t cap) {
    api::LogFile file(path);
    const auto count = file.size();

    control_need(count > 0 && count <= cap, "control owned-file extent");
    std::string bytes;

    for (std::uint64_t at = 0; at < count;) {
        auto chunk = file.read(at);
        control_need(!chunk.empty() && chunk.size() <= count - at,
                     "control file changed during read");
        bytes += chunk;
        at += chunk.size();
    }

    api::LogFile again(path);
    control_need(again.identity() == file.identity() && again.size() == count,
                 "control file replaced");
    std::string verify;

    for (std::uint64_t at = 0; at < count;) {
        auto chunk = again.read(at);
        control_need(!chunk.empty() && chunk.size() <= count - at,
                     "control file changed on recheck");
        verify += chunk;
        at += chunk.size();
    }

    control_need(bytes == verify, "control file contents raced");

    return bytes;
}

Json control_command(const std::string &bytes, const std::string &nonce) {
    std::set<std::string> keys;
    auto command = Json::parse(bytes, [&](int, Json::parse_event_t event, Json &value) {
        if (event == Json::parse_event_t::key) {
            control_need(keys.insert(value.get<std::string>()).second, "duplicate control key");
        }

        return true;
    });

    control_need(command.is_object() && command.size() == 5 &&
                         keys == std::set<std::string>{"schema", "nonce", "op", "frame_sequence",
                                                       "frame_sha256"},
                 "closed control schema");
    control_need(command.at("schema") == 1 && command.at("nonce") == nonce &&
                         command.at("op") == "exercise-speed-soc",
                 "foreign control nonce or operation");
    control_need(command.at("frame_sequence").is_number_unsigned() &&
                         command.at("frame_sequence").get<std::uint64_t>() > 0 &&
                         command.at("frame_sha256").is_string(),
                 "control frame types");
    const auto digest = command.at("frame_sha256").get<std::string>();
    control_need(digest.size() == 64 &&
                         digest.find_first_not_of("0123456789abcdef") == std::string::npos,
                 "control frame digest");

    return command;
}

void control_remember(PostFrameControl &control, const Json &result) {
    if (!control.enabled || !result.contains("latest_frame_export")) {
        return;
    }

    const auto &frame = result.at("latest_frame_export");

    if (!control.history.empty() && control.history.back() == frame) {
        return;
    }

    control.history.push_back(frame);

    if (control.history.size() > 16) {
        control.history.erase(control.history.begin());
    }
}

void control_save_frame(const std::filesystem::path &output, const char *name, const Json &frame) {
    const auto bytes = control_owned_bytes(output / "cluster.png", 8ULL << 20);

    control_need(sha256_bytes(bytes) == frame.at("png_sha256").get<std::string>(),
                 "control export no longer current");
    const auto target = output / name;
    control_need(!std::filesystem::exists(target), "control evidence already exists");
    voyemu::output::write_artifact(
            target, std::span(reinterpret_cast<const std::uint8_t *>(bytes.data()), bytes.size()));
    write_private_json(output / (std::string(name) + ".json"), frame);
}

template <class Handle>
void control_restore(PostFrameControl &c, const Json &status, std::uint64_t ms, Handle &handle) {
    const auto &qnx = status.at("deliveries").at("qnx");

    control_need(status.at("qnx_pid") == c.pid && qnx.at("generation") == c.generation &&
                         status.at("revision") == c.revision,
                 "control restore ownership/revision changed");
    Json values = Json::object();
    const auto &raw = status.at("state").at("raw_oem");

    for (const auto &[name, value] : c.baseline_raw.items()) {
        if (raw.at(name) != value) {
            control_need(!name.starts_with("swm_"), "unexpected wheel field changed");
            values[name] = value;
        }
    }

    control_need(!values.empty(), "control restore has no changed fields");
    const auto restored = handle(Json{{"op", "patch"},
                                      {"values", values},
                                      {"expected_revision", c.revision},
                                      {"expected_generation", c.generation}});
    control_need(restored.value("ok", false), "control restore rejected");
    c.revision = restored.at("revision");
    c.restore_sent = true;
    c.restore_sent_ms = ms;
    c.phase = "restore_pending";
    c.receipt["restore"] = {
            {"revision", c.revision},
            {"sent_ms", ms},
            {"raw_matches_before", restored.at("state").at("raw_oem") == c.baseline_raw}};
}

template <class Handle>
void control_tick(PostFrameControl &c, const std::filesystem::path &output, const Json &status,
                  const Json &result, std::uint64_t ms, std::uint64_t bound_ms, Handle &handle) {
    if (!c.enabled) {
        return;
    }

    control_remember(c, result);
    const auto &qnx = status.at("deliveries").at("qnx");

    if (!c.consumed) {
        const auto path = output / "post-frame-command.json";

        if (!std::filesystem::exists(path)) {
            return;
        }

        c.consumed = true;
        c.phase = "validating";
        const auto command = control_command(control_owned_bytes(path, 2048), c.nonce);
        control_need(bound_ms > ms + 35000, "insufficient original run budget for control/restore");
        auto known = std::find_if(c.history.begin(), c.history.end(), [&](const auto &f) {
            return f.at("sequence") == command.at("frame_sequence") &&
                   f.at("png_sha256") == command.at("frame_sha256");
        });
        control_need(known != c.history.end() && result.contains("frame_owner") &&
                             known->at("frame_owner") == result.at("frame_owner"),
                     "reviewed baseline not an owned exported frame");
        const auto reviewed = control_owned_bytes(output / "control-baseline.png", 8ULL << 20);
        control_need(sha256_bytes(reviewed) == command.at("frame_sha256").get<std::string>(),
                     "reviewed baseline image mismatch");
        control_need(qnx.at("vehicle").value("status", std::string{}) == "guest_acknowledged",
                     "baseline not acknowledged");
        c.pid = status.at("qnx_pid");
        c.generation = qnx.at("generation");
        c.baseline_frame = *known;
        c.baseline = status;
        c.baseline_raw = status.at("state").at("raw_oem");
        const Json values = {{"speed", c.baseline_raw.at("speed") == 37 ? 51 : 37},
                             {"soc", c.baseline_raw.at("soc") == 65 ? 49 : 65}};
        const auto patched = handle(Json{{"op", "patch"},
                                         {"values", values},
                                         {"expected_revision", status.at("revision")},
                                         {"expected_generation", c.generation}});
        control_need(patched.value("ok", false), "control patch rejected");
        c.revision = patched.at("revision");
        c.patch_sent = true;
        c.patch_sent_ms = ms;
        c.patch_until = ms + std::min<std::uint64_t>(120000, bound_ms - ms - 30000);
        c.restore_until = bound_ms - 1000;
        c.phase = "patch_pending";
        c.receipt = {{"nonce", c.nonce},
                     {"baseline_frame", c.baseline_frame},
                     {"patch", {{"values", values}, {"revision", c.revision}, {"sent_ms", ms}}},
                     {"visual_effect_requires_review", true}};

        return;
    }

    if (c.phase == "complete" || c.phase == "failed") {
        return;
    }

    control_need(status.at("qnx_pid") == c.pid && qnx.at("generation") == c.generation &&
                         status.at("revision") == c.revision,
                 "control live ownership/revision changed");
    const bool ack = qnx.at("vehicle").value("status", std::string{}) == "guest_acknowledged" &&
                     qnx.at("vehicle").value("sequence", std::uint64_t{}) >= c.revision;
    const Json frame = result.value("latest_frame_export", Json::object());
    const bool owner = frame.is_object() && frame.contains("frame_owner") &&
                       frame.at("frame_owner") == c.baseline_frame.at("frame_owner");

    if (c.phase == "patch_pending") {
        if (ack && !c.patch_ack_seen) {
            c.patch_ack_seen = true;
            c.patch_first_ack_ms = ms;
            c.patch_ack_frame_sequence = frame.value("sequence", std::uint64_t{});
            c.receipt["patch"]["first_ack_ms"] = ms;
            c.receipt["patch"]["frame_sequence_at_ack"] = c.patch_ack_frame_sequence;
        }

        if (ack && c.patch_ack_seen && owner && ms > c.patch_first_ack_ms &&
            frame.at("sequence").get<std::uint64_t>() > c.patch_ack_frame_sequence &&
            ms >= c.patch_sent_ms + 5000 &&
            frame.at("observed_ms").get<std::uint64_t>() > c.patch_first_ack_ms &&
            frame.at("sequence") > c.baseline_frame.at("sequence") &&
            frame.at("png_sha256") != c.baseline_frame.at("png_sha256")) {
            control_save_frame(output, "cluster-patched.png", frame);
            c.patch_frame = frame;
            c.patch_verified = true;
            c.receipt["patch"]["ack_sequence"] = qnx.at("vehicle").at("sequence");
            c.receipt["patch"]["frame"] = frame;
            control_restore(c, status, ms, handle);
        } else if (ms >= c.patch_until) {
            c.failure = "patch ACK/changed-frame deadline";
            control_restore(c, status, ms, handle);
        }
    } else if (c.phase == "restore_pending") {
        if (ack && !c.restore_ack_seen) {
            c.restore_ack_seen = true;
            c.restore_first_ack_ms = ms;
            c.restore_ack_frame_sequence = frame.value("sequence", std::uint64_t{});
            c.receipt["restore"]["first_ack_ms"] = ms;
            c.receipt["restore"]["frame_sequence_at_ack"] = c.restore_ack_frame_sequence;
        }

        if (ack && c.restore_ack_seen && owner && ms > c.restore_first_ack_ms &&
            frame.at("sequence").get<std::uint64_t>() > c.restore_ack_frame_sequence &&
            ms >= c.restore_sent_ms + 5000 &&
            frame.at("observed_ms").get<std::uint64_t>() > c.restore_first_ack_ms &&
            frame.at("sequence") >
                    c.patch_frame.value("sequence",
                                        c.baseline_frame.at("sequence").get<std::uint64_t>()) &&
            frame.at("png_sha256") !=
                    c.patch_frame.value("png_sha256",
                                        c.baseline_frame.at("png_sha256").get<std::string>())) {
            control_need(status.at("state").at("raw_oem") == c.baseline_raw,
                         "restore raw state differs");
            control_save_frame(output, "cluster-restored.png", frame);
            c.receipt["restore"]["ack_sequence"] = qnx.at("vehicle").at("sequence");
            c.receipt["restore"]["frame"] = frame;
            c.receipt["restore"]["raw_state_verified"] = true;
            c.receipt["restore"]["frame_after_ack"] = true;
            c.phase = c.patch_verified && c.failure.empty() ? "complete" : "failed";
        } else if (ms >= c.restore_until) {
            c.failure = "restore ACK/frame deadline";
            c.phase = "failed";
        }
    }
}

void control_protocol_test() {
    PostFrameControl empty;

    control_need(empty.receipt.value("restore", Json::object()).is_object() &&
                         empty.patch_frame.value("sequence", std::uint64_t{}) == 0,
                 "missing-command/timeout receipt defaults");
    const auto good = Json{{"schema", 1},
                           {"nonce", "own"},
                           {"op", "exercise-speed-soc"},
                           {"frame_sequence", std::uint64_t{1}},
                           {"frame_sha256", std::string(64, 'a')}};
    control_need(control_command(good.dump(), "own") == good, "control protocol positive");

    for (auto bad : {Json{{"schema", 1}}, good}) {
        if (bad == good) {
            bad["nonce"] = "foreign";
        }

        bool rejected = false;
        try {
            control_command(bad.dump(), "own");
        } catch (...) {
            rejected = true;
        }

        control_need(rejected, "control protocol rejected invalid input");
    }

    bool rejected = false;
    try {
        control_command("{\"schema\":1,\"schema\":1}", "own");
    } catch (...) {
        rejected = true;
    }

    control_need(rejected, "control duplicate rejected");
    PostFrameControl c;
    c.enabled = true;
    Json result;

    for (unsigned i = 1; i < 20; ++i) {
        result["latest_frame_export"] = {{"sequence", i}};
        control_remember(c, result);
    }

    control_need(c.history.size() == 16 && c.history.front().at("sequence") == 4,
                 "control bounded history");

    {
        PostFrameControl trial;
        trial.enabled = true;
        trial.consumed = true;
        trial.patch_sent = true;
        trial.phase = "patch_pending";
        trial.pid = 123;
        trial.generation = 2;
        trial.revision = 8;
        trial.patch_until = 1000;
        trial.restore_until = 10000;
        trial.baseline_raw = {{"speed", 0}, {"speed10x", 0}, {"soc", 80}};
        trial.baseline_frame = {{"sequence", std::uint64_t{4}},
                                {"png_sha256", std::string(64, 'a')},
                                {"frame_owner", {{"producer_address", 555}}}};
        Json status = {{"qnx_pid", 123}, {"revision", std::uint64_t{8}}};
        status["state"]["raw_oem"] = {{"speed", 37}, {"speed10x", 370}, {"soc", 65}};
        status["deliveries"]["qnx"] = {
                {"generation", 2},
                {"vehicle", {{"status", "queued"}, {"sequence", std::uint64_t{8}}}}};
        Json exported = {{"latest_frame_export",
                          {{"sequence", std::uint64_t{5}},
                           {"png_sha256", std::string(64, 'b')},
                           {"observed_ms", 900},
                           {"frame_owner", {{"producer_address", 555}}}}}};
        unsigned calls = 0;
        Json request;
        auto handle = [&](const Json &value) {
            ++calls;
            request = value;
            auto restored = status;
            restored["ok"] = true;
            restored["revision"] = std::uint64_t{9};
            restored["state"]["raw_oem"] = trial.baseline_raw;

            return restored;
        };
        control_tick(trial, {}, status, exported, 999, 10000, handle);
        control_need(calls == 0 && trial.phase == "patch_pending",
                     "frame without current ACK must not pass");
        control_tick(trial, {}, status, exported, 1000, 10000, handle);
        control_need(calls == 1 && trial.phase == "restore_pending" &&
                             request.at("values") == trial.baseline_raw &&
                             request.at("expected_revision") == 8 &&
                             request.at("expected_generation") == 2,
                     "deadline restores exactly owned changed fields");
        status["revision"] = std::uint64_t{9};
        status["state"]["raw_oem"] = trial.baseline_raw;
        control_tick(trial, {}, status, exported, 6000, 10000, handle);
        control_need(calls == 1 && trial.phase == "restore_pending",
                     "old/queued ACK must not prove restore");
        auto stale = status;
        stale["revision"] = std::uint64_t{10};
        bool race = false;
        try {
            control_tick(trial, {}, stale, exported, 7000, 10000, handle);
        } catch (...) {
            race = true;
        }

        control_need(race && calls == 1, "revision race rejects without overwrite");
        control_tick(trial, {}, status, exported, 10000, 11000, handle);
        control_need(trial.phase == "failed" && calls == 1, "restore deadline stays failed");
        PostFrameControl disabled;
        control_tick(disabled, {}, Json{}, Json{}, 0, 1, handle);
        control_need(calls == 1, "default disabled has no side effects");
        trial.restore_sent = false;
        auto foreign = status;
        foreign["qnx_pid"] = 124;
        bool owner = false;
        try {
            control_restore(trial, foreign, 10001, handle);
        } catch (...) {
            owner = true;
        }

        control_need(owner && calls == 1, "foreign PID cannot restore");
    }

    for (const char *phase : {"waiting_command", "validating", "patch_pending", "restore_pending",
                              "failed", "complete"}) {
        PostFrameControl gate;
        gate.enabled = true;
        gate.phase = phase;
        control_need(control_terminal(gate) == (gate.phase == "complete" || gate.phase == "failed"),
                     "control terminal gate");
        control_need(control_success(gate) == (gate.phase == "complete"),
                     "requested incomplete control must fail exit");
        gate.enabled = false;
        control_need(!control_terminal(gate) && control_success(gate),
                     "default control gates unchanged");
    }

    std::cout
            << Json{{"control_protocol_test", true},
                    {"VM_started", false},
                    {"scope",
                     "parser/history plus mocked ACK/deadline/restore/owner races; ordinary-file and PNG positive runtime tests remain required"}}
                       .dump()
            << '\n';
}

void snapshot_test(const std::filesystem::path &directory) {
    if (!directory.is_absolute() || std::filesystem::exists(directory)) {
        throw std::runtime_error("new absolute snapshot test directory required");
    }

    ensure_private_directory(directory);
    const Json owner = {{"schema", 1},
                        {"source", "original-OEM-Screen-BMP"},
                        {"producer_address", 0x50000000ULL}},
               producers = Json::array({0x50000000ULL});
    Json result = {{"actual_exported_frame", false}};
    auto need = [](bool yes) {
        if (!yes) {
            throw std::runtime_error("snapshot cadence/receipt regression");
        }
    };
    {
        frames::Writer writer(directory / "frame.bgra");
        std::vector<std::byte> pixels(frames::pixel_bytes, std::byte{0});
        writer.publish(pixels, 1, 100);
        capture_probe_frame(directory, directory, producers, result, true, 0);
        need(!result.value("actual_exported_frame", false));
        write_private_json(directory / "frame-owner.json", owner);
        capture_probe_frame(directory, directory, producers, result, true, 0);
        need(result.at("first_frame_export").at("sequence") == 1 &&
             result.at("latest_frame_export").at("sequence") == 1 &&
             result.at("snapshot_encode_count") == 1);
        const auto first = sha256_file(directory / "cluster-first.png"),
                   old_latest = sha256_file(directory / "cluster.png");
        pixels[0] = std::byte{255};
        writer.publish(pixels, 2, 200);

        for (unsigned i = 1; i <= 25; ++i) {
            capture_probe_frame(directory, directory, producers, result, false,
                                static_cast<std::uint64_t>(i) * 20);
        }

        need(result.at("observed_frame_sequence") == 2 && result.at("frame_sequence") == 1 &&
             result.at("latest_frame_export").at("sequence") == 1 &&
             result.at("snapshot_encode_count") == 1 &&
             sha256_file(directory / "cluster.png") == old_latest);
        capture_probe_frame(directory, directory, producers, result, true, 20000);
        need(result.at("latest_frame_export").at("sequence") == 2 &&
             result.at("latest_frame_export").at("timestamp_ns") == 200 &&
             result.at("latest_frame_export").at("frame_owner") == owner &&
             result.at("snapshot_encode_count") == 2);
        need(sha256_file(directory / "cluster-first.png") == first &&
             sha256_file(directory / "cluster.png") != old_latest &&
             sha256_file(directory / "cluster.png") ==
                     result.at("latest_frame_export").at("png_sha256").get<std::string>());
        capture_probe_frame(directory, directory, producers, result, true, 40000);
        need(result.at("snapshot_encode_count") == 2);
        pixels[4] = std::byte{127};
        writer.publish(pixels, 3, 300);
        capture_probe_frame(directory, directory, Json::array({0x60000000ULL}), result, true,
                            60000);
        need(result.at("latest_frame_export").at("sequence") == 2 &&
             result.at("snapshot_encode_count") == 2);
        capture_probe_frame(directory, directory, producers, result, true, 60000);
        need(result.at("latest_frame_export").at("sequence") == 3 &&
             result.at("snapshot_encode_count") == 3);
        need(read_private_json(directory / "cluster.json") == result.at("latest_frame_export") &&
             read_private_json(directory / "cluster-first.json") ==
                     result.at("first_frame_export"));
        {
            std::ofstream changed(directory / "cluster.png", std::ios::binary | std::ios::trunc);
            changed << "foreign change";
        }
        const auto changed = sha256_file(directory / "cluster.png");
        pixels[8] = std::byte{64};
        writer.publish(pixels, 4, 400);
        bool rejected = false;
        try {
            capture_probe_frame(directory, directory, producers, result, true, 80000);
        } catch (const std::exception &) {
            rejected = true;
        }

        need(rejected && sha256_file(directory / "cluster.png") == changed &&
             sha256_file(directory / "cluster-first.png") == first);
    }
    std::filesystem::remove_all(directory);
    std::cout << Json{{"synthetic_snapshot_test", true},
                      {"VM_started", false},
                      {"first_preserved", true},
                      {"between_progress_no_encode", true},
                      {"latest_sequence_and_owner_exact", true},
                      {"foreign_change_rejected", true}}
                         .dump()
              << '\n';
}
} // namespace

int main(int argc, char **argv) {
    try {
        if (argc == 2 && std::string_view(argv[1]) == "--control-protocol-test") {
            control_protocol_test();

            return 0;
        }

        if (argc == 2 && std::string_view(argv[1]) == "--target-priority-test") {
            progress_target_priority_test();
            std::cout << Json{{"target_priority_test", true}, {"VM_started", false}}.dump() << '\n';

            return 0;
        }

        if (argc == 3 && std::string_view(argv[1]) == "--snapshot-test") {
            snapshot_test(argv[2]);

            return 0;
        }

        if (argc == 3 && std::string_view(argv[1]) == "--file-identity") {
            Json value;
            auto file = CreateFileW(std::filesystem::path(argv[2]).c_str(),
                                    FILE_READ_ATTRIBUTES | READ_CONTROL,
                                    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                                    OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr);

            if (file == INVALID_HANDLE_VALUE) {
                throw std::runtime_error("file identity open " + std::to_string(GetLastError()));
            }

            BY_HANDLE_FILE_INFORMATION info{};
            value["information_ok"] = bool(GetFileInformationByHandle(file, &info));
            value["links"] = info.nNumberOfLinks;
            value["attributes"] = info.dwFileAttributes;
            value["bytes"] = (std::uint64_t(info.nFileSizeHigh) << 32) | info.nFileSizeLow;
            HANDLE token = nullptr;
            value["token_open"] = bool(OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token));
            std::vector<std::byte> owner_bytes, user_bytes;
            auto token_info = [&](TOKEN_INFORMATION_CLASS kind, std::vector<std::byte> &bytes) {
                DWORD count = 0;
                GetTokenInformation(token, kind, nullptr, 0, &count);
                bytes.resize(count);

                return bool(GetTokenInformation(token, kind, bytes.data(), count, &count));
            };
            const bool owner_ok = token && token_info(TokenOwner, owner_bytes),
                       user_ok = token && token_info(TokenUser, user_bytes);
            PSID owner = nullptr;
            PSECURITY_DESCRIPTOR descriptor = nullptr;
            value["security_status"] = GetSecurityInfo(file, SE_FILE_OBJECT,
                                                       OWNER_SECURITY_INFORMATION, &owner, nullptr,
                                                       nullptr, nullptr, &descriptor);
            value["owner_equal_token_owner"] =
                    owner && owner_ok &&
                    EqualSid(owner, reinterpret_cast<TOKEN_OWNER *>(owner_bytes.data())->Owner);
            value["owner_equal_token_user"] =
                    owner && user_ok &&
                    EqualSid(owner, reinterpret_cast<TOKEN_USER *>(user_bytes.data())->User.Sid);

            if (descriptor) {
                LocalFree(descriptor);
            }

            if (token) {
                CloseHandle(token);
            }

            CloseHandle(file);
            std::cout << value.dump() << '\n';

            return 0;
        }

        if (argc == 3 && std::string_view(argv[1]) == "--path-attributes") {
            Json rows = Json::array();

            for (auto path = std::filesystem::absolute(argv[2]).lexically_normal(); !path.empty();
                 path = path.parent_path()) {
                const auto attributes = GetFileAttributesW(path.c_str());
                const auto error = attributes == INVALID_FILE_ATTRIBUTES ? GetLastError() : 0;
                rows.push_back(
                        {{"path", path.string()}, {"attributes", attributes}, {"error", error}});

                if (path == path.parent_path()) {
                    break;
                }
            }

            std::cout << rows.dump() << '\n';

            return 0;
        }

        if (argc != 5 && argc != 6) {
            throw std::runtime_error("usage: ROOT PROFILE LAUNCH.json RESULT.json [SECONDS]");
        }

        unsigned seconds = 60;

        if (argc == 6) {
            const std::string_view value = argv[5];
            const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(),
                                                      seconds);

            if (error != std::errc{} || end != value.data() + value.size() || seconds < 1 ||
                seconds > 3600) {
                throw std::runtime_error("probe seconds must be an integer in 1..3600");
            }
        }

        const std::string profile = argv[2];

        if (!valid_profile_name(profile)) {
            throw std::runtime_error("profile required");
        }

        const auto root = std::filesystem::canonical(argv[1]),
                   launch = std::filesystem::canonical(argv[3]);
        const auto result_file = std::filesystem::absolute(argv[4]).lexically_normal();
        const auto relative = launch.lexically_relative(root / ".local/build/tmp");

        if (relative.empty() || relative.is_absolute()) {
            throw std::runtime_error("project probe launch required");
        }

        for (const auto &part : relative) {
            if (part == "..") {
                throw std::runtime_error("probe launch leaves project");
            }
        }

        if (result_file.parent_path() != launch.parent_path() ||
            std::filesystem::exists(result_file)) {
            throw std::runtime_error("new adjacent probe result required");
        }

        const auto progress_file = result_file.parent_path() / "progress.jsonl";

        if (std::filesystem::exists(progress_file)) {
            throw std::runtime_error("new adjacent probe progress required");
        }

        const auto spec = read_private_json(launch, 128 * 1024);

        if (spec.at("ram_backend") != "file") {
            throw std::runtime_error("owned file RAM recipe required");
        }

        if (!probe_disk_reserve_available(std::filesystem::space(root).available)) {
            throw std::runtime_error("disk reserve reached");
        }

        const auto token = random_token();
        const auto directory = root / ".local/machines/qnx-process-probe/sessions" / token;
        ensure_private_directory(directory);
        Session session(directory, token);
        PostFrameControl control;
        control.enabled = spec.value("developer_post_frame_control", false);
        control.nonce = token;
        control.phase = control.enabled ? "waiting_command" : "disabled";

        if (control.enabled) {
            if (std::filesystem::exists(result_file.parent_path() / "post-frame-command.json") ||
                std::filesystem::exists(result_file.parent_path() / "control-baseline.png")) {
                throw std::runtime_error("new control namespace required");
            }

            write_private_json(result_file.parent_path() / "post-frame-control-info.json",
                               {{"schema", 1},
                                {"nonce", token},
                                {"session", directory.string()},
                                {"command", "post-frame-command.json"},
                                {"reviewed_baseline", "control-baseline.png"},
                                {"one_shot", true},
                                {"fixed_operation", "exercise-speed-soc"}});
        }

        Json result = {{"scope", "bounded Windows QNX process-RAM supervisor trace"},
                       {"session", directory.string()},
                       {"actual_exported_frame", false},
                       {"guest_ack", false},
                       {"bounded_seconds", seconds},
                       {"peer_touched", false}};
        try {
            const auto definition = read_private_json(root / "profiles" / profile / "profile.json",
                                                      1 << 20);
            const auto initial = definition.value("initial_state", Json::object());

            if (!initial.empty()) {
                const auto seeded = session.handle({{"op", "patch"}, {"values", initial}});

                if (!seeded.value("ok", false)) {
                    throw std::runtime_error("profile startup patch failed");
                }
            }

            session.configure_cluster(spec, seconds);
            session.attach_cluster(spec, seconds);
            const auto started = std::chrono::steady_clock::now(),
                       deadline = started + std::chrono::seconds(seconds + 5);
            std::vector<QnxBootProcessTarget> targets;
            unsigned progress_number = 0;
            std::ofstream progress(progress_file, std::ios::binary);

            if (!progress) {
                throw std::runtime_error("probe progress open failed");
            }

            auto next_progress = started;

            while (std::chrono::steady_clock::now() < deadline) {
                session.tick();
                const auto status = session.handle({{"op", "status"}});
                const auto &qnx = status.at("deliveries").at("qnx");

                result["live_qnx"] = qnx;
                result["qnx_pid"] = status.at("qnx_pid");
                result["guest_ack"] = qnx.contains("vehicle") &&
                                      qnx.at("vehicle").value("status", std::string{}) ==
                                              "guest_acknowledged";
                const auto sampled = std::chrono::steady_clock::now();
                capture_probe_frame(
                        directory, result_file.parent_path(), qnx.value("producers", Json::array()),
                        result, sampled >= next_progress,
                        std::chrono::duration_cast<std::chrono::milliseconds>(sampled - started)
                                .count());

                if (sampled >= next_progress) {
                    if (control.enabled) {
                        auto handle = [&](const Json &request) {
                            return session.handle(request);
                        };
                        try {
                            control_tick(control, result_file.parent_path(), status, result,
                                         std::chrono::duration_cast<std::chrono::milliseconds>(
                                                 sampled - started)
                                                 .count(),
                                         std::uint64_t(seconds) * 1000, handle);
                        } catch (const std::exception &error) {
                            control.failure = error.what();
                            control.phase = "failed";

                            if (control.patch_sent && !control.restore_sent) {
                                try {
                                    control_restore(
                                            control, session.handle({{"op", "status"}}),
                                            std::chrono::duration_cast<std::chrono::milliseconds>(
                                                    sampled - started)
                                                    .count(),
                                            handle);
                                } catch (const std::exception &restore_error) {
                                    control.receipt["restore_error"] = restore_error.what();
                                    control.phase = "failed";
                                }
                            }
                        }

                        result["post_frame_control"] = control.receipt;
                        result["post_frame_control"]["phase"] = control.phase;
                        result["post_frame_control"]["control_evidence_complete"] = control.phase ==
                                                                                    "complete";
                        result["post_frame_control"]["failure"] = control.failure;
                        write_private_json(result_file.parent_path() /
                                                   "post-frame-control-receipt.json",
                                           result["post_frame_control"]);

                        if (control_terminal(control)) {
                            result["control_terminal_trigger"] = control.phase;
                            break;
                        }
                    }

                    const bool discover = progress_number++ % 3 == 0 || targets.empty();
                    const auto observed = read_qnx_boot_progress(
                            [&](std::uint64_t physical, std::span<std::byte> output) {
                                return session.read_owned_qnx_ram(physical, output);
                            },
                            discover ? std::span<const QnxBootProcessTarget>{}
                                     : std::span<const QnxBootProcessTarget>{targets});

                    if (discover && observed.value("status", std::string{}) == "verified") {
                        targets.clear();

                        for (const auto &process : observed.at("processes")) {
                            const auto name = process.at("name").get<std::string>();

                            if (progress_target_priority(name) < progress_target_names.size()) {
                                targets.push_back({process.at("pid").get<std::uint32_t>(), name});
                            }
                        }

                        std::stable_sort(targets.begin(), targets.end(),
                                         [&](const auto &a, const auto &b) {
                                             return progress_target_priority(a.name) <
                                                    progress_target_priority(b.name);
                                         });

                        if (targets.size() > 16) {
                            targets.resize(16);
                        }
                    }

                    Json sample = {{"elapsed_ms",
                                    std::chrono::duration_cast<std::chrono::milliseconds>(sampled -
                                                                                          started)
                                            .count()},
                                   {"qnx_status", qnx.value("status", std::string{})},
                                   {"kernel_boot_progress", observed},
                                   {"guest_ack", result["guest_ack"]},
                                   {"actual_exported_frame", result["actual_exported_frame"]}};

                    if (qnx.contains("producers")) {
                        sample["producers"] = qnx["producers"];
                    }

                    if (result.contains("frame_owner")) {
                        sample["frame_owner"] = result["frame_owner"];
                    }

                    for (const auto *key : {"first_frame_export", "latest_frame_export"}) {
                        if (result.contains(key)) {
                            sample[key] = result[key];
                        }
                    }

                    progress << sample.dump() << '\n' << std::flush;

                    if (!progress) {
                        throw std::runtime_error("probe progress write failed");
                    }

                    std::cout << "QNX_PROGRESS " << sample.dump() << std::endl;
                    result["last_progress"] = sample;
                    next_progress = sampled + std::chrono::seconds(20);

                    if (!probe_disk_reserve_available(std::filesystem::space(root).available)) {
                        throw std::runtime_error("disk reserve reached");
                    }
                }

                const auto state = qnx.value("status", std::string{});

                if (state == "bounded_run_complete" || state == "exited" ||
                    state == "graphics_worker_exited") {
                    break;
                }

                std::this_thread::sleep_for(std::chrono::milliseconds(20));
            }

            result["elapsed_ms"] = std::chrono::duration_cast<std::chrono::milliseconds>(
                                           std::chrono::steady_clock::now() - started)
                                           .count();
            capture_probe_frame(directory, result_file.parent_path(),
                                result.at("live_qnx").value("producers", Json::array()), result,
                                true, result["elapsed_ms"].get<std::uint64_t>());
        } catch (const std::exception &error) {
            result["error"] = error.what();
        }

        if (control.enabled) {
            if (control.phase != "complete" && control.phase != "failed") {
                control.failure = "requested control incomplete at original run terminal/deadline";
                control.phase = "failed";
            }

            result["post_frame_control"] = control.receipt;
            result["post_frame_control"]["phase"] = control.phase;
            result["post_frame_control"]["failure"] = control.failure;
            result["post_frame_control"]["control_evidence_complete"] = control_success(control);
            result["post_frame_control"]["visual_effect_requires_review"] = true;
            const auto restore = control.receipt.value("restore", Json::object());
            result["post_frame_control"]["restore_ack_frame_verified_at_stop"] =
                    restore.value("raw_state_verified", false) &&
                    restore.value("frame_after_ack", false);
            try {
                write_private_json(result_file.parent_path() / "post-frame-control-receipt.json",
                                   result["post_frame_control"]);
            } catch (const std::exception &error) {
                control.phase = "failed";
                result["error"] = std::string("control terminal receipt: ") + error.what();
                result["post_frame_control"]["phase"] = "failed";
                result["post_frame_control"]["control_evidence_complete"] = false;
            }
        }

        session.stop();
        const auto terminal = session.handle({{"op", "status"}});
        result["terminal_qnx"] = terminal.at("deliveries").at("qnx");
        result["stopped"] = terminal.at("stopped");
        result["ram_file_exists"] = std::filesystem::exists(directory / "qnx-ram.bin");
        write_private_json(result_file, result);
        std::cout << result.dump() << '\n';

        return result.contains("error") || !control_success(control) ? 1 : 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';

        return 1;
    }
}
