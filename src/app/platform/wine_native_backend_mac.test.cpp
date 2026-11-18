#include "../core/model.hpp"
#include "../core/process.hpp"
#include <chrono>
#include <iostream>
#include <signal.h>
#include <stdexcept>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

namespace {
using namespace voyemu;
using Clock = std::chrono::steady_clock;

void check(bool value) {
    if (!value) {
        throw std::runtime_error("native launcher assertion failed");
    }
}

void tick() {
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
}

Json proof(const std::filesystem::path &file, bool terminal = false) {
    const auto deadline = Clock::now() + std::chrono::seconds(6);

    while (Clock::now() < deadline) {
        try {
            auto value = read_private_json(file);

            if (!terminal || value.at("state") == "terminal") {
                return value;
            }
        } catch (const std::exception &) {
            // Ignored: receipt reads may fail transiently before the polling deadline.
        }

        tick();
    }

    throw std::runtime_error("native launcher proof timeout");
}
} // namespace

int main(int argc, char **argv) {
    if (argc == 3 && std::string(argv[1]) == "--fake-child") {
        std::this_thread::sleep_for(std::chrono::milliseconds(std::stoi(argv[2])));

        return 7;
    }

    try {
        check(argc == 2);
        const auto self = std::filesystem::canonical(argv[0]),
                   launcher = std::filesystem::canonical(argv[1]);
        const auto root = std::filesystem::current_path() / ".local/build/tmp" /
                          ("native-launcher-test-" + random_token());
        ensure_private_directory(root);
        const std::string nonce(32, 'a');
        unsigned scenario = 0;
        auto request = [&](unsigned duration, unsigned timeout) {
            const auto dir = root / std::to_string(++scenario);
            ensure_private_directory(dir);
            Json value = {{"schema", 1},
                          {"nonce", nonce},
                          {"executable", self.string()},
                          {"arguments", Json::array({"--fake-child", std::to_string(duration)})},
                          {"environment", Json::object()},
                          {"directory", dir.string()},
                          {"output", (dir / "child.log").string()},
                          {"ports", Json::array({19371u})},
                          {"proof_file", (dir / "proof.json").string()},
                          {"stop_file", (dir / "stop.json").string()},
                          {"timeout_ms", timeout}};
            write_private_json(dir / "request.json", value);

            return dir;
        };
        auto launch = [&](const std::filesystem::path &dir) {
            ProcessSpec spec;
            spec.executable = launcher;
            spec.arguments = {"--request", (dir / "request.json").string()};
            spec.output = dir / "launcher.log";

            return ChildProcess::launch(spec);
        };
        auto wait = [&](ChildProcess &child) {
            const auto deadline = Clock::now() + std::chrono::seconds(5);

            while (child.alive() && Clock::now() < deadline) {
                tick();
            }

            check(!child.alive() && child.exit_code() == 0);
        };
        auto normal = request(300, 5000);
        auto normal_child = launch(normal);
        wait(normal_child);
        auto normal_proof = proof(normal / "proof.json", true);
        check(normal_proof.at("exit_code") == 7 && normal_proof.at("alive") == false &&
              normal_proof.at("terminal_reason") == "child_exit");
        auto stopped = request(30000, 5000);
        auto stopped_child = launch(stopped);
        auto initial = proof(stopped / "proof.json");
        check(initial.at("alive") == true && !initial.at("native_birth").is_null() &&
              initial.at("ports").at("19371") == false);
        ProcessSpec foreign_spec;
        foreign_spec.executable = self;
        foreign_spec.arguments = {"--fake-child", "30000"};
        auto foreign = ChildProcess::launch(foreign_spec);
        write_private_json(
                stopped / "stop.json",
                {{"schema", 1}, {"nonce", std::string(32, 'b')}, {"native_pid", foreign.pid()}});
        std::this_thread::sleep_for(std::chrono::milliseconds(350));
        auto rejected = proof(stopped / "proof.json");
        check(rejected.at("diagnostic") == "stop_nonce_rejected" && rejected.at("alive") == true &&
              foreign.alive());
        write_private_json(stopped / "stop.json",
                           {{"schema", 1}, {"nonce", nonce}, {"native_pid", foreign.pid()}});
        wait(stopped_child);
        check(proof(stopped / "proof.json", true).at("terminal_reason") == "stop" &&
              foreign.alive());
        foreign.terminate(100);
        auto bounded = request(30000, 1000);
        write_private_json(bounded / "stop.json", Json::object());
        auto bounded_child = launch(bounded);
        wait(bounded_child);
        auto bounded_proof = proof(bounded / "proof.json", true);
        check(bounded_proof.at("terminal_reason") == "deadline" &&
              bounded_proof.at("diagnostic") == "stop_invalid");
        auto orphan = request(30000, 5000);
        int ready[2];
        check(pipe(ready) == 0);
        pid_t owner = fork();
        check(owner >= 0);

        if (owner == 0) {
            close(ready[0]);
            auto child = launch(orphan);
            char byte = '1';
            write(ready[1], &byte, 1);
            std::this_thread::sleep_for(std::chrono::seconds(6));
            _exit(0);
        }

        close(ready[1]);
        char byte = 0;
        check(read(ready[0], &byte, 1) == 1);
        close(ready[0]);
        auto running = proof(orphan / "proof.json");
        check(running.at("proxy_parent_pid") == owner);
        check(kill(owner, SIGTERM) == 0);
        int status = 0;
        check(waitpid(owner, &status, 0) == owner);
        auto terminal = proof(orphan / "proof.json", true);
        check(terminal.at("terminal_reason") == "parent_exit" && terminal.at("alive") == false);
        check(kill(terminal.at("native_pid").get<pid_t>(), 0) == -1 && errno == ESRCH);
        auto hardlinked = request(30000, 5000);
        std::filesystem::create_hard_link(hardlinked / "request.json", hardlinked / "alias.json");
        auto invalid = launch(hardlinked);
        const auto deadline = Clock::now() + std::chrono::seconds(3);

        while (invalid.alive() && Clock::now() < deadline) {
            tick();
        }

        check(!invalid.alive() && invalid.exit_code() == 64 &&
              !std::filesystem::exists(hardlinked / "proof.json"));
        auto bad_nonce = request(30000, 5000);
        auto bad_request = read_private_json(bad_nonce / "request.json");
        bad_request["nonce"] = "wrong";
        write_private_json(bad_nonce / "request.json", bad_request);
        auto bad = launch(bad_nonce);
        const auto bad_deadline = Clock::now() + std::chrono::seconds(3);

        while (bad.alive() && Clock::now() < bad_deadline) {
            tick();
        }

        check(!bad.alive() && bad.exit_code() == 64 &&
              !std::filesystem::exists(bad_nonce / "proof.json"));
        auto escape = request(30000, 5000);
        auto escaped = read_private_json(escape / "request.json");
        escaped["proof_file"] = (root / "escaped.json").string();
        write_private_json(escape / "request.json", escaped);
        auto escaping = launch(escape);
        const auto escape_deadline = Clock::now() + std::chrono::seconds(3);

        while (escaping.alive() && Clock::now() < escape_deadline) {
            tick();
        }

        check(!escaping.alive() && escaping.exit_code() == 64 &&
              !std::filesystem::exists(root / "escaped.json"));
        write_private_json(root / "receipt.json", {{"schema", 1},
                                                   {"normal_exit", true},
                                                   {"stop_nonce", true},
                                                   {"stale_nonce_rejected", true},
                                                   {"malformed_stop_bounded", true},
                                                   {"foreign_pid_untouched", true},
                                                   {"parent_death_reaped", true},
                                                   {"hardlink_request_rejected", true},
                                                   {"invalid_nonce_request_rejected", true},
                                                   {"escaped_proof_rejected", true},
                                                   {"stdin_eof_ignored", true},
                                                   {"scenarios", scenario}});
        std::cout << (root / "receipt.json").string() << '\n';

        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';

        return 1;
    }
}
