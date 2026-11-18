#include "../core/model.hpp"
#include "../core/process.hpp"
#include <chrono>
#include <iostream>
#include <libproc.h>
#include <stdexcept>
#include <sys/event.h>
#include <sys/stat.h>
#include <unistd.h>

namespace {
using namespace voyemu;
using Clock = std::chrono::steady_clock;

void need(bool value) {
    if (!value) {
        throw voyemu::VoyemuError("NATIVE_BACKEND_REQUEST", "invalid private launch request");
    }
}

std::string string(const Json &value) {
    need(value.is_string());
    auto text = value.get<std::string>();
    need(text.size() <= 65536 && text.find('\0') == std::string::npos);

    return text;
}

std::filesystem::path path(const Json &value) {
    auto text = string(value);
    std::filesystem::path result(text);

    need(!text.empty() && result.is_absolute() && result == result.lexically_normal());

    return result;
}

Json birth(pid_t pid) {
    proc_bsdinfo info{};

    if (proc_pidinfo(pid, PROC_PIDTBSDINFO, 0, &info, sizeof info) != sizeof info) {
        return nullptr;
    }

    return {{"seconds", info.pbi_start_tvsec}, {"microseconds", info.pbi_start_tvusec}};
}

void private_file(const std::filesystem::path &file, std::size_t maximum) {
    struct stat info{};

    need(lstat(file.c_str(), &info) == 0 && S_ISREG(info.st_mode) && info.st_uid == geteuid() &&
         info.st_nlink == 1 && (info.st_mode & 0077) == 0 && info.st_size >= 0 &&
         static_cast<std::uint64_t>(info.st_size) <= maximum);
}

struct Queue {
    int fd = kqueue();

    ~Queue() {
        if (fd >= 0) {
            close(fd);
        }
    }
};

int run(const std::filesystem::path &request_file) {
    need(request_file.is_absolute());
    private_file(request_file, 1 << 20);
    const auto request = read_private_json(request_file, 1 << 20);
    need(request.is_object() && request.at("schema") == 1);
    const auto nonce = string(request.at("nonce"));
    need(nonce.size() == 32 &&
         nonce.find_first_not_of("0123456789abcdefABCDEF") == std::string::npos);
    const auto parent_directory = std::filesystem::canonical(request_file.parent_path());
    struct stat directory_info{};
    need(stat(parent_directory.c_str(), &directory_info) == 0 &&
         directory_info.st_uid == geteuid() && (directory_info.st_mode & 0077) == 0);
    ensure_private_directory(parent_directory);
    const auto proof = path(request.at("proof_file")), stop = path(request.at("stop_file"));
    need(proof.parent_path() == parent_directory && stop.parent_path() == parent_directory &&
         proof != stop && proof != request_file && stop != request_file);
    ProcessSpec spec;
    spec.executable = path(request.at("executable"));
    spec.directory = path(request.at("directory"));
    spec.output = path(request.at("output"));
    need(spec.output != proof && spec.output != stop && spec.output != request_file);
    const auto &arguments = request.at("arguments");
    need(arguments.is_array() && arguments.size() <= 512);

    for (const auto &value : arguments) {
        spec.arguments.push_back(string(value));
    }

    const auto &environment = request.at("environment");
    need(environment.is_object() && environment.size() <= 512);

    for (auto it = environment.begin(); it != environment.end(); ++it) {
        need(!it.key().empty() && it.key().size() <= 65536 &&
             it.key().find_first_of("=\0", 0, 2) == std::string::npos);
        spec.environment[it.key()] = string(it.value());
    }

    const auto &ports = request.at("ports");
    need(ports.is_array() && ports.size() <= 8);

    for (const auto &port : ports) {
        need(port.is_number_unsigned() && port.get<std::uint64_t>() >= 1024 &&
             port.get<std::uint64_t>() <= 65535);
    }

    const auto &timeout = request.at("timeout_ms");
    need(timeout.is_number_unsigned() && timeout.get<std::uint64_t>() >= 1000 &&
         timeout.get<std::uint64_t>() <= 7200000);
    struct stat executable_info{};
    need(stat(spec.executable.c_str(), &executable_info) == 0 && S_ISREG(executable_info.st_mode));
    const auto parent = getppid();
    const auto parent_birth = birth(parent);
    need(parent > 1 && !parent_birth.is_null());
    Queue queue;
    need(queue.fd >= 0);
    struct kevent event{};
    EV_SET(&event, parent, EVFILT_PROC, EV_ADD | EV_ONESHOT, NOTE_EXIT, 0, nullptr);
    need(kevent(queue.fd, &event, 1, nullptr, 0, nullptr) == 0);
    need(getppid() == parent && birth(parent) == parent_birth);
    timespec immediate{};
    need(kevent(queue.fd, nullptr, 0, &event, 1, &immediate) == 0);
    auto child = ChildProcess::launch(spec);
    const auto native_birth = birth(static_cast<pid_t>(child.pid()));
    const auto deadline = Clock::now() + std::chrono::milliseconds(timeout.get<std::uint64_t>());
    std::string diagnostic = "none", reason = "child_exit";
    auto publish = [&](bool alive) {
        Json listeners = Json::object();

        for (const auto &value : ports) {
            auto port = value.get<unsigned>();
            listeners[std::to_string(port)] = alive && child.owns_tcp_listener(port);
        }

        alive = child.alive();
        write_private_json(proof, {{"schema", 1},
                                   {"nonce", nonce},
                                   {"proxy_parent_pid", parent},
                                   {"parent_birth", parent_birth},
                                   {"native_pid", child.pid()},
                                   {"native_birth", native_birth},
                                   {"executable", spec.executable.string()},
                                   {"executable_device", executable_info.st_dev},
                                   {"executable_inode", executable_info.st_ino},
                                   {"alive", alive},
                                   {"ports", listeners},
                                   {"observed_utc_ms",
                                    std::chrono::duration_cast<std::chrono::milliseconds>(
                                            std::chrono::system_clock::now().time_since_epoch())
                                            .count()},
                                   {"state", alive ? "running" : "terminal"},
                                   {"exit_code", child.exit_code()},
                                   {"diagnostic", diagnostic},
                                   {"terminal_reason", alive ? "none" : reason}});
    };
    try {
        while (child.alive()) {
            if (getppid() != parent || birth(parent) != parent_birth) {
                reason = "parent_exit";
                break;
            }

            if (Clock::now() >= deadline) {
                reason = "deadline";
                break;
            }

            if (std::filesystem::exists(stop) || std::filesystem::is_symlink(stop)) {
                try {
                    private_file(stop, 4096);
                    auto command = read_private_json(stop, 4096);

                    if (command.is_object() && command.value("schema", 0) == 1 &&
                        command.contains("nonce") && command.at("nonce").is_string()) {
                        if (command.at("nonce") == nonce) {
                            reason = "stop";
                            break;
                        }

                        diagnostic = "stop_nonce_rejected";
                    } else {
                        diagnostic = "stop_invalid";
                    }
                } catch (const std::exception &) {
                    diagnostic = "stop_invalid";
                }
            }

            publish(true);
            timespec wait{0, 100000000};
            const int count = kevent(queue.fd, nullptr, 0, &event, 1, &wait);

            if (count < 0) {
                reason = "parent_watch_error";
                break;
            }

            if (count > 0) {
                reason = "parent_exit";
                break;
            }
        }

        child.terminate(500);
        publish(false);
    } catch (...) {
        child.terminate(500);
        throw;
    }

    return 0;
}
} // namespace

int main(int argc, char **argv) {
    try {
        if (argc != 3 || std::string(argv[1]) != "--request") {
            throw voyemu::VoyemuError("NATIVE_BACKEND_USAGE", "--request UNIXPATH required");
        }

        return run(argv[2]);
    } catch (const std::exception &) {
        std::cerr << "NATIVE_BACKEND_FAILED: private request or owned lifecycle failed\n";

        return 64;
    }
}
