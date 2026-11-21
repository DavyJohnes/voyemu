#include "provider_registry.hpp"
#include "os/platform.hpp"
#include "provider_process.hpp"
#include "provider_registry_test_host.hpp"
#include <cassert>
#include <fstream>
#include <iostream>
using namespace voyemu;
using namespace voyemu::runtime;
namespace {
Json response(const Json &payload) {
    return {{"schema", 1},
            {"request_id", payload.at("request_id")},
            {"source", payload.at("source_id")},
            {"status", "ready"},
            {"boot_id", payload.at("session").at("boot_id")},
            {"source_instance", "instance"},
            {"coverage", "sampled"},
            {"revision", "revision"},
            {"observed", {{"clock", "host-monotonic-ns"}, {"start", "1"}, {"end", "2"}}},
            {"data", {{"value", 0}}}};
}
} // namespace

int main(int argc, char **argv) {
    try {
        // The Windows self-worker answers a request file with a response file.
        if (os::host_windows && argc == 6 && std::string(argv[2]) == "--request" &&
            std::string(argv[4]) == "--response") {
            const auto payload = read_private_json(std::filesystem::u8path(argv[3]), 16384);
            write_private_json(std::filesystem::u8path(argv[5]), response(payload));

            return 0;
        }

        const auto base = argc == 2 ? std::filesystem::canonical(argv[1])
                                    : std::filesystem::canonical(std::filesystem::current_path());
        const auto root = base / ("provider-test-" + random_token());
        ensure_private_directory(root);
        struct Cleanup {
            std::filesystem::path path;

            ~Cleanup() {
                std::error_code error;
                std::filesystem::remove_all(path, error);
            }
        } cleanup{root};
        const auto jobs = root / "jobs";
        ensure_provider_jobs_directory(jobs);
        ensure_provider_jobs_directory(jobs);
        const auto file = root / "worker";
        write_private_json(file, "pinned");
        Json pinned = {{"path", file.string()}, {"sha256", sha256_file(file)}};
        bool file_jobs_rejected = false;
        try {
            ensure_provider_jobs_directory(file);
        } catch (const std::exception &error) {
            file_jobs_rejected = true;
            assert(os::host_windows ||
                   std::string(error.what()) == "PROVIDER_JOBS: provider job limit reached");
        }

        assert(file_jobs_rejected && sha256_file(file) == pinned["sha256"].get<std::string>());
        Json manifest = {{"schema", 1},       {"id", "example"},
                         {"version", "1"},    {"worker", pinned},
                         {"runtime", pinned}, {"capabilities", {{"opaque", true}}}};
        ProviderRegistry registry(jobs);
        registry.load(manifest);
        assert(registry.contains("example"));
        auto rejects = [&](Json bad) {
            ProviderRegistry fresh(jobs);
            bool failed = false;
            try {
                fresh.load(bad);
            } catch (...) {
                failed = true;
            }

            assert(failed);
        };
        auto bad = manifest;
        bad["id"] = "ui";
        rejects(bad);
        bad = manifest;
        bad["argv"] = Json::array();
        rejects(bad);
        bad = manifest;
        bad["worker"]["sha256"] = std::string(64, '0');
        rejects(bad);
        bad = manifest;
        bad["runtime"]["sha256"] = std::string(64, '0');
        rejects(bad);
        bad = manifest;
        bad["files"] =
                Json::array({Json{{"path", file.string()}, {"sha256", std::string(64, '0')}}});
        rejects(bad);
        bool duplicate = false;
        try {
            registry.load(manifest);
        } catch (...) {
            duplicate = true;
        }

        assert(duplicate);
        const auto link = root / "link";
        std::filesystem::create_symlink(file, link);
        bad = manifest;
        bad["worker"]["path"] = link.string();
        rejects(bad);
        std::filesystem::remove(link);
        std::filesystem::create_hard_link(file, link);
        rejects(manifest);
        std::filesystem::remove(link);
        const auto linked_parent = root / "linked-parent";
        std::filesystem::create_directory_symlink(root, linked_parent);
        bad = manifest;
        bad["worker"]["path"] = (linked_parent / "worker").string();
        rejects(bad);
        std::filesystem::remove(linked_parent);
        bad = manifest;
        bad["worker"]["path"] = (root / "." / "worker").string();
        rejects(bad);
        ProviderContext context{"request", "session", "profile", "serial", "boot"};
        auto result = [&](const ProcessSpec &spec, int milliseconds) {
            assert(milliseconds > 0 && milliseconds <= 5000);
            assert(spec.arguments.size() == 5 && spec.arguments[1] == "--request" &&
                   spec.arguments[3] == "--response");
            const auto payload = read_private_json(std::filesystem::u8path(spec.arguments[2]),
                                                   16384);
            assert(payload.size() == 6 && payload["capabilities"] == manifest["capabilities"]);
            write_private_json(std::filesystem::u8path(spec.arguments[4]), response(payload));

            return ProcessResult{0, "", false};
        };
        auto check_clean = [&]() {
            assert(std::filesystem::is_empty(jobs));
        };
        assert(registry.collect("example", context, 5000, result, [] {
            return true;
        })["status"] == "ready");
        check_clean();
        assert(registry.collect("unknown", context, 5000, result, [] {
            return true;
        })["status"] == "unavailable");
        assert(registry.collect("example", context, 0, result, [] {
            return true;
        })["status"] == "unavailable");

        for (unsigned mode = 0; mode < 9; ++mode) {
            ProviderRunner malformed = [&](const ProcessSpec &spec, int milliseconds) {
                auto execution = result(spec, milliseconds);
                const auto output = std::filesystem::u8path(spec.arguments[4]);

                if (mode == 0) {
                    return ProcessResult{0, "", true};
                }

                if (mode == 1) {
                    std::ofstream stream(output);
                    stream << "{";
                }

                if (mode == 2) {
                    std::ofstream stream(output);
                    stream << std::string(1200001, 'x');
                }

                if (mode == 3) {
                    std::filesystem::remove(output);
                    std::filesystem::create_symlink(file, output);
                }

                if (mode >= 4 && mode <= 6) {
                    auto reply = read_private_json(output);
                    reply[mode == 4 ? "boot_id" : mode == 5 ? "request_id" : "source"] = "other";
                    write_private_json(output, reply);
                }

                if (mode == 7) {
                    std::filesystem::remove(output);
                    std::filesystem::create_hard_link(file, output);
                }

                if (mode == 8) {
                    const auto reply = read_private_json(output);
                    std::filesystem::remove(output);
                    public_result(output, reply);
                }

                return execution;
            };
            assert(registry.collect("example", context, 5000, malformed, [] {
                return true;
            })["status"] == "unavailable");
            check_clean();
        }

        int fences = 0;
        const auto first_fence = [&] {
            return ++fences == 1;
        };
        assert(registry.collect("example", context, 5000, result, first_fence)["status"] ==
               "unavailable");
        check_clean();
        pinned_self_worker_case(manifest, std::filesystem::canonical(argv[0]), root, jobs, context,
                                check_clean);
        std::cout
                << (os::host_windows
                            ? "PASS Windows pinned manifest to real self-worker collect, filesystem "
                              "guards and result/fence negatives\n"
                            : "PASS provider registry guards and result/fence negatives\n");

        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';

        return 1;
    }
}
