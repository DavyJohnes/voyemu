#include "qnx_host_source.hpp"
#include <chrono>
#include <iostream>
#include <stdexcept>

namespace {
using namespace voyemu;

void check(bool value, const char *message) {
    if (!value) {
        throw std::runtime_error(message);
    }
}

template <typename Work> void rejects(Work work, const char *message) {
    try {
        work();
    } catch (const VoyemuError &error) {
        check(error.code() == "QNX_HOST", "source engine failure code differs");

        return;
    }

    throw std::runtime_error(message);
}
} // namespace

int main() {
    try {
        const auto jobs = std::filesystem::current_path() / ".local" / "build" / "jobs";
        std::filesystem::create_directories(jobs);
        const auto root =
                std::filesystem::canonical(jobs) /
                ("voyemu-qnx-host-source-" +
                 std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directory(root);
        struct Cleanup {
            std::filesystem::path root;

            ~Cleanup() {
                std::error_code error;
                std::filesystem::remove_all(root, error);
            }
        } cleanup{root};
        const Json recipe = {{"version", "11.1.2"}, {"source", "synthetic"}};
        const QnxHostSourceEngine host(root, recipe, "linux");
        check(host.target.parent_path() == root / ".local/toolchains" &&
                      host.target.filename() ==
                              "qnx-host-linux-11.1.2-" + sha256_bytes(recipe.dump()).substr(0, 12),
              "source engine directory differs");
        check(!host.installed(), "absent source engine reported installed");
        ensure_private_directory(host.parent);
        const auto payload = root / "payload";
        ensure_private_directory(payload);
        qnx_host_text(payload / "qemu-system-aarch64", "engine");
        const auto published = host.publish(payload, payload / "qemu-system-aarch64");
        check(published.at("cached") == false && published.at("version") == "11.1.2" &&
                      published.at("executable") == host.binary.string(),
              "published source engine result differs");
        check(!std::filesystem::exists(payload) && host.installed() &&
                      host.result(true).at("cached") == true,
              "published source engine is not installed");
        const auto receipt = read_private_json(host.target / "qnx-host.json");
        check(receipt.at("component_sha256") == host.pin && receipt.at("recipe") == recipe &&
                      receipt.at("executable_sha256") == sha256_bytes("engine"),
              "source engine receipt differs");
        // A second build must not replace the published engine.
        ensure_private_directory(payload);
        qnx_host_text(payload / "qemu-system-aarch64", "other");
        rejects(
                [&] {
                    host.publish(payload, payload / "qemu-system-aarch64");
                },
                "source engine replaced");
        qnx_host_text(host.binary, "changed");
        rejects(
                [&] {
                    host.installed();
                },
                "changed source engine accepted");
        std::cout << "source engine directory, receipt and publication contracts passed\n";

        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';

        return 1;
    }
}
