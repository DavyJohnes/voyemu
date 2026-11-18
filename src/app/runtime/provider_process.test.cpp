#include "provider_process.hpp"
#include <cassert>
#include <fstream>
#include <iostream>
#include <stdexcept>
#ifdef _WIN32
#include <windows.h>
#endif
#ifndef _WIN32
#include <signal.h>
#include <unistd.h>
#endif
using namespace voyemu;
using namespace voyemu::runtime;

int main(int argc, char **argv) try {
#ifdef _WIN32
    if (argc == 2 && std::string(argv[1]) == "--descendant") {
        Sleep(30000);

        return 0;
    }

    if (argc == 2 && std::string(argv[1]) == "--nonzero") {
        return 7;
    }

    if (argc == 3 && (std::string(argv[1]) == "--worker" || std::string(argv[1]) == "--timeout")) {
        std::wstring current(32768, L'\0');
        const auto length = GetModuleFileNameW(nullptr, current.data(),
                                               static_cast<DWORD>(current.size()));

        if (!length || length >= current.size()) {
            return 90;
        }

        current.resize(length);
        auto command = L"\"" + current + L"\" --descendant";
        STARTUPINFOW startup{};
        startup.cb = sizeof startup;
        PROCESS_INFORMATION process{};

        if (!CreateProcessW(current.c_str(), command.data(), nullptr, nullptr, FALSE,
                            CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process)) {
            return 91;
        }

        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
        {
            std::ofstream output(std::filesystem::u8path(argv[2]));
            output << process.dwProcessId;

            if (!output) {
                return 92;
            }
        }

        if (std::string(argv[1]) == "--timeout") {
            Sleep(30000);
        }

        return 0;
    }

    auto check = [](bool value, const char *message) {
        if (!value) {
            throw std::runtime_error(message);
        }
    };
    check(argc == 2, "provider fixture root required");
    const auto root = std::filesystem::path(argv[1]) / ("provider-runtime-" + random_token());
    ensure_private_directory(root);
    struct Cleanup {
        std::filesystem::path root;

        ~Cleanup() {
            std::error_code error;
            std::filesystem::remove_all(root, error);
        }
    } cleanup{root};
    ProcessSpec worker;
    worker.executable = std::filesystem::canonical(argv[0]);
    worker.directory = root;
    worker.output = root / "worker.log";
    auto descendant_stopped = [&](const std::filesystem::path &file) {
        DWORD pid = 0;
        std::ifstream(file) >> pid;
        check(pid > 0, "worker descendant PID published");
        const auto process = OpenProcess(SYNCHRONIZE, FALSE, pid);

        if (!process) {
            check(GetLastError() == ERROR_INVALID_PARAMETER, "descendant process query failed");

            return;
        }

        const auto wait = WaitForSingleObject(process, 3000);
        CloseHandle(process);
        check(wait == WAIT_OBJECT_0, "owned job descendant still running");
    };
    worker.arguments = {"--worker", (root / "normal-pid").string()};
    auto result = run_provider(worker, 3000);
    check(result.exit_code == 0 && !result.timed_out, "Windows provider normal exit");
    descendant_stopped(root / "normal-pid");
    worker.arguments = {"--nonzero"};
    result = run_provider(worker, 3000);
    check(result.exit_code == 7 && !result.timed_out, "Windows provider nonzero exit preserved");
    worker.arguments = {"--timeout", (root / "timeout-pid").string()};
    const auto began = std::chrono::steady_clock::now();
    result = run_provider(worker, 1000);
    check(result.timed_out && std::chrono::steady_clock::now() - began < std::chrono::seconds(3),
          "Windows provider deadline bounded");
    descendant_stopped(root / "timeout-pid");

    for (const auto bound : {0, 10001}) {
        bool rejected = false;
        try {
            run_provider(worker, bound);
        } catch (...) {
            rejected = true;
        }

        check(rejected, "Windows provider invalid timeout rejected");
    }

    worker.detached = true;
    bool rejected = false;
    try {
        run_provider(worker, 1000);
    } catch (...) {
        rejected = true;
    }

    check(rejected, "Windows provider detached lifetime rejected");
#endif
#ifndef _WIN32
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
#endif
    auto sample = provider_sample({{"status", "ready"},
                                   {"source_instance", "s"},
                                   {"boot_id", "b"},
                                   {"revision", "r"},
                                   {"data", {{"enabled", false}}}});
    assert(sample.status == "ready" && !sample.continuous &&
           sample.value["data"]["enabled"] == false);
    assert(provider_sample({{"status", "absent"}}).status == "ready");
    assert(provider_sample({{"status", "unavailable"}}).status == "unavailable");
} catch (const std::exception &error) {
    std::cerr << error.what() << '\n';

    return 1;
}
