#include "model.hpp"
#include "provider_process.hpp"
#include "provider_process_test_host.hpp"
#include <windows.h>
#include <chrono>
#include <fstream>
#include <stdexcept>
#include <string>
using namespace voyemu;
using namespace voyemu::runtime;

std::optional<int> provider_child_mode(int argc, char **argv) {
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

    return std::nullopt;
}

void provider_lifecycle_case(int argc, char **argv) {
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
}
