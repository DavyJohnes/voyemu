#define main qnx_process_probe_application_main
#include "qnx_windows_process_probe.cpp"
#undef main

void control_file_positive_test(const std::filesystem::path &directory) {
    control_need(directory.is_absolute() && directory.lexically_normal() == directory &&
                         !std::filesystem::exists(directory),
                 "fresh absolute control fixture required");
    ensure_private_directory(directory);
    const auto session = directory / "session", output = directory / "output";
    ensure_private_directory(session);
    ensure_private_directory(output);
    PostFrameControl control;
    control.enabled = true;
    control.phase = "waiting_command";
    control.nonce = "synthetic-host-file-fixture";
    Json state = initial_state(), before = state.at("raw_oem"), status;
    auto update = [&](const char *delivery, std::uint64_t sequence) {
        status = {{"ok", true},
                  {"qnx_pid", 123},
                  {"revision", state.at("sequence")},
                  {"state", state}};
        status["deliveries"]["qnx"] = {{"generation", std::uint64_t{2}},
                                       {"vehicle", {{"status", delivery}, {"sequence", sequence}}}};
    };
    update("guest_acknowledged", state.at("sequence").get<std::uint64_t>());
    unsigned patches = 0;
    auto handle = [&](const Json &request) {
        control_need(request.at("op") == "patch" &&
                             request.at("expected_revision") == state.at("sequence") &&
                             request.at("expected_generation") == 2,
                     "mock same-Session request guards");
        ++patches;
        state = apply_patch(state, request.at("values"));
        update("queued", state.at("sequence").get<std::uint64_t>());

        return status;
    };
    const Json owner = {{"schema", 1},
                        {"source", "original-OEM-Screen-BMP"},
                        {"producer_address", std::uint64_t{0x50000000}}},
               producers = Json::array({std::uint64_t{0x50000000}});
    Json result = {{"actual_exported_frame", false}};
    {
        frames::Writer writer(session / "frame.bgra");
        std::vector<std::byte> pixels(frames::pixel_bytes, std::byte{0});
        write_private_json(session / "frame-owner.json", owner);
        writer.publish(pixels, 1, 100);
        capture_probe_frame(session, output, producers, result, true, 100000);
        control_remember(control, result);
        const auto image = control_owned_bytes(output / "cluster.png", 8ULL << 20);
        voyemu::output::write_artifact(
                output / "control-baseline.png",
                std::span(reinterpret_cast<const std::uint8_t *>(image.data()), image.size()));
        write_private_json(output / "post-frame-command.json",
                           {{"schema", 1},
                            {"nonce", control.nonce},
                            {"op", "exercise-speed-soc"},
                            {"frame_sequence", std::uint64_t{1}},
                            {"frame_sha256", sha256_bytes(image)}});
        control_tick(control, output, status, result, 100000, 1000000, handle);
        control_need(patches == 1 && control.phase == "patch_pending",
                     "actual owned command/PNG route did not patch");
        pixels[0] = std::byte{255};
        writer.publish(pixels, 2, 200);
        capture_probe_frame(session, output, producers, result, true, 105000);
        update("guest_acknowledged", 0);
        control_tick(control, output, status, result, 105000, 1000000, handle);
        control_need(patches == 1 && control.phase == "patch_pending" &&
                             !std::filesystem::exists(output / "cluster-patched.png"),
                     "old ACK passed with actual changed PNG");
        update("guest_acknowledged", state.at("sequence").get<std::uint64_t>());
        control_tick(control, output, status, result, 105001, 1000000, handle);
        control_need(patches == 1 && control.patch_ack_seen &&
                             !std::filesystem::exists(output / "cluster-patched.png"),
                     "delayed ACK accepted pre-ACK frame");
        result["latest_frame_export"]["observed_ms"] = 105002;
        control_tick(control, output, status, result, 105002, 1000000, handle);
        control_need(patches == 1, "inflated observation time accepted old frame sequence");
        writer.publish(pixels, 3, 250);
        capture_probe_frame(session, output, producers, result, true, 105003);
        control_tick(control, output, status, result, 105003, 1000000, handle);
        control_need(patches == 2 && control.phase == "restore_pending" &&
                             state.at("raw_oem") == before,
                     "restore not sent through guarded mock");
        pixels[0] = std::byte{0};
        writer.publish(pixels, 4, 300);
        capture_probe_frame(session, output, producers, result, true, 110002);
        update("guest_acknowledged", state.at("sequence").get<std::uint64_t>());
        control_tick(control, output, status, result, 110002, 1000000, handle);
        control_need(control.phase == "restore_pending" && control.restore_ack_seen &&
                             !std::filesystem::exists(output / "cluster-restored.png"),
                     "restore ACK accepted pre-ACK frame");
        result["latest_frame_export"]["observed_ms"] = 110003;
        control_tick(control, output, status, result, 110003, 1000000, handle);
        control_need(control.phase == "restore_pending",
                     "inflated restore timestamp accepted old sequence");
        writer.publish(pixels, 5, 350);
        capture_probe_frame(session, output, producers, result, true, 110004);
        control_tick(control, output, status, result, 110004, 1000000, handle);
        control_need(control_success(control) && control_terminal(control) &&
                             control.phase == "complete" && patches == 2,
                     "actual PNG patch/restore sequence incomplete");
        control_need(sha256_file(output / "cluster-patched.png") == control.receipt.at("patch")
                                                                            .at("frame")
                                                                            .at("png_sha256")
                                                                            .get<std::string>() &&
                             sha256_file(output / "cluster-restored.png") ==
                                     control.receipt.at("restore")
                                             .at("frame")
                                             .at("png_sha256")
                                             .get<std::string>(),
                     "preserved actual PNG receipts differ");
        control_tick(control, output, status, result, 110003, 1000000, handle);
        control_need(patches == 2, "consumed one-shot mutated twice");
        bool rejected = false;
        try {
            control_owned_bytes(output, 2048);
        } catch (...) {
            rejected = true;
        }

        control_need(rejected, "ordinary reader accepted a directory");
        const auto oversized = output / "oversized";
        {
            std::ofstream file(oversized, std::ios::binary);
            file << std::string(2049, 'x');
        }
        rejected = false;
        try {
            control_owned_bytes(oversized, 2048);
        } catch (...) {
            rejected = true;
        }

        control_need(rejected, "ordinary reader accepted oversized file");
        const auto link = output / "linked.png";
        std::filesystem::create_hard_link(output / "control-baseline.png", link);
        rejected = false;
        try {
            control_owned_bytes(link, 8ULL << 20);
        } catch (...) {
            rejected = true;
        }

        control_need(rejected, "ordinary reader accepted a hardlink");
        std::filesystem::remove(link);
    }
    std::filesystem::remove_all(directory);
    std::cout << Json{{"VM_started", false},
                      {"ordinary_file_and_synthetic_PNG_positive", true},
                      {"same_Session_mock", true},
                      {"actual_OEM_PNG_verified", false},
                      {"visual_effect_verified", false},
                      {"double_read_race_verified", false},
                      {"hardlink_oversize_directory_rejected", true}}
                         .dump()
              << '\n';
}

int main(int argc, char **argv) {
    try {
        probe_disk_reserve_test();
        control_protocol_test();

        if (argc == 3 && std::string_view(argv[1]) == "--file-control-test") {
            control_file_positive_test(std::filesystem::absolute(argv[2]).lexically_normal());
        } else {
            control_need(argc == 1, "usage [--file-control-test fresh-directory]");
        }

        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';

        return 1;
    }
}
