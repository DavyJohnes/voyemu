#include "model.hpp"
#include "provider_process.hpp"
#include "provider_process_test_host.hpp"
#include <cassert>
#include <chrono>
#include <fstream>
#include <signal.h>
#include <string>
#include <unistd.h>
using namespace voyemu;
using namespace voyemu::runtime;

std::optional<int> provider_child_mode(int argc, char **argv) {
    if (argc == 3 && std::string(argv[1]) == "--worker") {
        auto child = fork();

        if (child == 0) {
            while (true) {
                pause();
            }
        }

        std::ofstream(argv[2]) << child;

        return 0;
    }

    if (argc == 2 && std::string(argv[1]) == "--timeout") {
        while (true) {
            pause();
        }
    }

    return std::nullopt;
}

void provider_lifecycle_case(int argc, char **argv) {
    assert(argc == 2);
    const auto root = std::filesystem::path(argv[1]) /
                      std::filesystem::path("provider-runtime-" + random_token());
    std::filesystem::create_directory(root);
    struct Cleanup {
        std::filesystem::path root;

        ~Cleanup() {
            std::filesystem::remove_all(root);
        }
    } cleanup{root};
    ProcessSpec worker;
    worker.executable = std::filesystem::canonical(argv[0]);
    worker.directory = root;
    worker.output = root / "worker.log";
    worker.arguments = {"--worker", (root / "pid").string()};
    auto result = run_provider(worker, 1000);
    assert(result.exit_code == 0 && !result.timed_out);
    pid_t descendant = 0;
    std::ifstream(root / "pid") >> descendant;
    assert(descendant > 0);
    // SIGKILL may leave a short-lived orphan zombie; it cannot execute further work.
    worker.arguments = {"--timeout"};
    auto began = std::chrono::steady_clock::now();
    result = run_provider(worker, 30);
    assert(result.timed_out && std::chrono::steady_clock::now() - began < std::chrono::seconds(1));
}
