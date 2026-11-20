#include "supervisor_stop.hpp"
#include "model.hpp"
#include <iostream>
using namespace voyemu;

static void require(bool value, const std::string &message) {
    if (!value) {
        throw std::runtime_error(message);
    }
}

// A supervisor stand-in: the orderly one stops at the request and records it, the stubborn one
// ignores it.
static int supervisor(const std::filesystem::path &session, bool orderly) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(60);

    while (std::chrono::steady_clock::now() < deadline) {
        if (orderly && supervisor_stop_requested(session)) {
            std::ofstream(session / "guests-stopped").close();

            return 0;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

    return 1;
}

int main(int argc, char **argv) try {
    if (argc == 3 && std::string_view(argv[1]) == "--orderly") {
        return supervisor(argv[2], true);
    }

    if (argc == 3 && std::string_view(argv[1]) == "--stubborn") {
        return supervisor(argv[2], false);
    }

    const auto root = std::filesystem::temp_directory_path() /
                      ("voyemu-supervisor-stop-" + random_token());
    ensure_private_directory(root);
    struct Cleanup {
        std::filesystem::path root;

        ~Cleanup() {
            std::filesystem::remove_all(root);
        }
    } cleanup{root};
    const auto launch = [&](const char *mode, const std::filesystem::path &session) {
        ensure_private_directory(session);
        ProcessSpec spec;
        spec.executable = std::filesystem::absolute(argv[0]);
        spec.arguments = {mode, session.string()};
        spec.directory = root;
        spec.output = session / "supervisor.log";
        spec.detached = true;
        auto child = ChildProcess::launch(spec);
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        require(child.alive(), std::string(mode) + " supervisor exited early");

        return child;
    };

    // Ctrl+C during start lets the supervisor stop its guests on every platform instead of killing
    // it and them at once.
    const auto orderly_session = root / "orderly";
    auto orderly = launch("--orderly", orderly_session);
    stop_starting_supervisor(orderly, orderly_session, std::chrono::seconds(30));
    require(!orderly.alive() && orderly.exit_code() == 0 &&
                    std::filesystem::exists(orderly_session / "guests-stopped"),
            "interrupted start killed the supervisor before its orderly stop");

    // A supervisor that ignores the request is killed once the grace period ends.
    const auto stubborn_session = root / "stubborn";
    auto stubborn = launch("--stubborn", stubborn_session);
    const auto requested = std::chrono::steady_clock::now();
    stop_starting_supervisor(stubborn, stubborn_session, std::chrono::milliseconds(500));
    const auto stopped = std::chrono::steady_clock::now() - requested;
    require(!stubborn.alive() && stopped >= std::chrono::milliseconds(500) &&
                    stopped < std::chrono::seconds(10),
            "stubborn supervisor was not killed after the grace period");
    std::cout << "interrupted start: orderly supervisor stop, kill after grace\n";

    return 0;
} catch (const std::exception &error) {
    std::cerr << error.what() << '\n';

    return 1;
}
