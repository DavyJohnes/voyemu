#include "wine_native_backend_windows.hpp"
#include "error.hpp"
#include "wine_native_proof_reader.hpp"
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <stdexcept>
#include <thread>

namespace voyemu {
namespace {
using Spawn = LONG(WINAPI *)(char *const *, int);
// Wine 11 kernel32/path.c: caller owns the process-heap buffer.
using UnixName = char *(__cdecl *)(const wchar_t *);

void need(bool condition) {
    if (!condition) {
        throw VoyemuError("DIAGNOSTIC_UNSUPPORTED", "invalid Wine native backend capability");
    }
}

Spawn spawn_api() {
    auto module = GetModuleHandleW(L"ntdll.dll");
    auto result = reinterpret_cast<Spawn>(GetProcAddress(module, "__wine_unix_spawnvp"));

    need(result);

    return result;
}

std::string unix_path(const std::filesystem::path &path) {
    need(path.is_absolute());
    auto api = reinterpret_cast<UnixName>(
            GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "wine_get_unix_file_name"));
    need(api);
    char *value = api(path.c_str());
    need(value);
    std::string result(value);
    HeapFree(GetProcessHeap(), 0, value);
    need(!result.empty() && result.front() == '/');

    return result;
}

LONG spawn(std::vector<std::string> values) {
    std::vector<char *> pointers;

    for (auto &value : values) {
        need(value.find('\0') == std::string::npos);
        pointers.push_back(value.data());
    }

    pointers.push_back(nullptr);

    return spawn_api()(pointers.data(), 1);
}

void permissions(const std::filesystem::path &path, const char *mode) {
    need(spawn({"/bin/chmod", mode, unix_path(path)}) == 0);
}

bool drive_path(const std::string &value) {
    return value.size() >= 3 &&
           ((value[0] >= 'A' && value[0] <= 'Z') || (value[0] >= 'a' && value[0] <= 'z')) &&
           value[1] == ':' && (value[2] == '\\' || value[2] == '/');
}

std::string argument(const std::string &value) {
    if (drive_path(value)) {
        return unix_path(std::filesystem::u8path(value));
    }

    for (const auto prefix : {std::string("loader,file="), std::string("file:"),
                              std::string("memory-backend-file,id=qnxmem,size=8192M,mem-path=")}) {
        if (value.starts_with(prefix)) {
            const auto end = value.find(',', prefix.size());
            const auto file = value.substr(prefix.size(),
                                           end == std::string::npos ? end : end - prefix.size());

            if (drive_path(file)) {
                return prefix + unix_path(std::filesystem::u8path(file)) +
                       (end == std::string::npos ? std::string{} : value.substr(end));
            }
        }
    }

    if (value.starts_with("memory-backend-file,")) {
        const auto field = value.find(",mem-path=");

        if (field != std::string::npos) {
            const auto begin = field + 10, end = value.find(',', begin);
            const auto file = value.substr(begin, end == std::string::npos ? end : end - begin);

            if (drive_path(file)) {
                return value.substr(0, begin) + unix_path(std::filesystem::u8path(file)) +
                       (end == std::string::npos ? std::string{} : value.substr(end));
            }
        }
    }

    return value;
}

std::int64_t now() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
                   std::chrono::system_clock::now().time_since_epoch())
            .count();
}

Json identity(const Json &p) {
    return {p.at("native_pid"),        p.at("native_birth"),     p.at("executable"),
            p.at("executable_device"), p.at("executable_inode"), p.at("proxy_parent_pid"),
            p.at("parent_birth")};
}
} // namespace

bool wine_native_host() {
    return GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "wine_get_version") != nullptr;
}

std::string wine_native_unix_path(const std::filesystem::path &path) {
    return unix_path(path);
}

std::filesystem::path wine_native_windows_path(std::string_view path) {
    need(!path.empty() && path.front() == '/' && path.find('\0') == std::string_view::npos);
    using DosName = wchar_t *(__cdecl *)(const char *);
    auto api = reinterpret_cast<DosName>(
            GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "wine_get_dos_file_name"));
    need(api);
    const std::string source(path);
    wchar_t *value = api(source.c_str());
    need(value);
    std::filesystem::path result(value);
    HeapFree(GetProcessHeap(), 0, value);
    need(result.is_absolute());

    return result;
}

std::unique_ptr<WineNativeBackendWindows>
WineNativeBackendWindows::launch(const ProcessSpec &spec) {
    spawn_api();
    need(spec.wine_native_diagnostic.has_value());
    const auto &options = spec.wine_native_diagnostic.value();
    need(spec.arguments.size() <= 512 && spec.environment.size() <= 512 && !spec.detached &&
         options.ports.size() <= 8 && options.timeout_ms >= 1000 && options.timeout_ms <= 7200000);

    for (auto port : options.ports) {
        need(port >= 1024 && port <= 65535);
    }

    auto result = std::make_unique<WineNativeBackendWindows>();
    result->nonce_ = random_token();
    const auto directory = options.request_directory / result->nonce_;
    need(!std::filesystem::exists(directory) && !std::filesystem::is_symlink(directory));
    ensure_private_directory(directory);
    permissions(directory, "700");
    const auto request_file = directory / "request.json";
    result->proof_ = directory / "proof.json";
    result->stop_ = directory / "stop.json";
    result->executable_ = unix_path(spec.executable);
    Json arguments = Json::array();

    for (const auto &value : spec.arguments) {
        arguments.push_back(argument(value));
    }

    Json environment = Json::object();

    for (const auto &[key, value] : spec.environment) {
        std::string upper = key;
        std::transform(upper.begin(), upper.end(), upper.begin(), [](unsigned char c) {
            return std::toupper(c);
        });

        if (upper == "PATH" || upper == "WINEPATH" || upper == "QT_PLUGIN_PATH" ||
            upper == "QT_QPA_PLATFORM_PLUGIN_PATH" || upper == "DYLD_LIBRARY_PATH" ||
            upper == "LD_LIBRARY_PATH") {
            continue;
        }

        environment[key] = argument(value);
    }

    Json request = {{"schema", 1},
                    {"nonce", result->nonce_},
                    {"launcher", unix_path(options.launcher)},
                    {"proof_file", unix_path(result->proof_)},
                    {"stop_file", unix_path(result->stop_)},
                    {"executable", result->executable_},
                    {"directory", unix_path(spec.directory)},
                    {"output", unix_path(spec.output)},
                    {"arguments", arguments},
                    {"environment", environment},
                    {"ports", options.ports},
                    {"timeout_ms", options.timeout_ms}};
    write_private_json(request_file, request, 1 << 20);
    permissions(request_file, "600");
    std::wstring current(32768, L'\0');
    auto length = GetModuleFileNameW(nullptr, current.data(), static_cast<DWORD>(current.size()));
    need(length > 0 && length < current.size());
    current.resize(length);
    ProcessSpec proxy;
    proxy.executable = current;
    proxy.arguments = {"_wine-native", "--request", request_file.string()};
    proxy.directory = directory;
    proxy.output = directory / "proxy.log";
    result->proxy_ = ChildProcess::launch(proxy);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);

    while (std::chrono::steady_clock::now() < deadline) {
        try {
            auto p = result->proof();
            need(p.at("native_pid").is_number_integer() &&
                 p.at("native_pid").get<std::int64_t>() > 1 && !p.at("native_birth").is_null() &&
                 p.at("executable") == result->executable_);
            result->pid_ = p.at("native_pid");
            result->identity_ = identity(p);

            return result;
        } catch (const std::exception &) { /* Ignored: an incomplete proof is polled again. */
        }

        if (!result->proxy_.alive()) {
            break;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

    result->proxy_.terminate(1000);
    throw VoyemuError("DIAGNOSTIC_UNSUPPORTED", "no valid native launch proof");
}

Json WineNativeBackendWindows::proof() const {
    detail::WineNativeProofReader snapshot(proof_);
    auto p = Json::parse(snapshot.text());

    need(p.is_object() && p.at("schema") == 1 && p.at("nonce") == nonce_);
    need(p.at("native_pid").is_number_integer() && p.at("native_pid").get<std::int64_t>() > 1 &&
         p.at("executable") == executable_ && p.at("executable_device").is_number_integer() &&
         p.at("executable_inode").is_number_integer());

    for (const auto *key : {"native_birth", "parent_birth"}) {
        const auto &birth = p.at(key);
        need(birth.is_object() && birth.at("seconds").is_number_integer() &&
             birth.at("seconds").get<std::int64_t>() > 0 &&
             birth.at("microseconds").is_number_integer() &&
             birth.at("microseconds").get<std::int64_t>() >= 0 &&
             birth.at("microseconds").get<std::int64_t>() < 1000000);
    }

    need(p.at("proxy_parent_pid").is_number_integer() &&
         p.at("proxy_parent_pid").get<std::int64_t>() > 1);
    const auto observed = p.at("observed_utc_ms").get<std::int64_t>(), time = now();
    const bool retained_terminal = p.at("state") == "terminal" && p.at("alive") == false &&
                                   !proxy_.alive();
    need(retained_terminal || (observed <= time + 1000 && time - observed <= 2000));

    if (!identity_.is_null()) {
        need(identity(p) == identity_);
    }

    return p;
}

bool WineNativeBackendWindows::alive() {
    if (!proxy_.alive()) {
        (void)exit_code();

        return false;
    }

    try {
        auto p = proof();

        if (p.at("alive") == false && p.at("state") == "terminal") {
            terminal_exit_ = p.at("exit_code").get<int>();
        }

        return p.at("alive") == true && p.at("state") == "running";
    } catch (const std::exception &) {
        return false;
    }
}

bool WineNativeBackendWindows::owns_tcp_listener(unsigned port) {
    if (!proxy_.alive()) {
        return false;
    }

    try {
        auto p = proof();

        return p.at("alive") == true && p.at("state") == "running" &&
               p.at("ports").value(std::to_string(port), false);
    } catch (const std::exception &) {
        return false;
    }
}

int WineNativeBackendWindows::exit_code() const {
    if (terminal_exit_) {
        return *terminal_exit_;
    }

    try {
        auto p = proof();

        if (p.at("state") == "terminal" && p.at("alive") == false) {
            terminal_exit_ = p.at("exit_code").get<int>();

            return *terminal_exit_;
        }
    } catch (const std::exception &) { /* Ignored: an unreadable proof reports no exit code. */
    }

    return -1;
}

void WineNativeBackendWindows::terminate(int grace_ms) {
    if (!proxy_.alive() && terminal_exit_) {
        return;
    }

    if (proxy_.alive()) {
        write_private_json(stop_, {{"schema", 1}, {"nonce", nonce_}}, 4096);
        permissions(stop_, "600");
        const auto deadline = std::chrono::steady_clock::now() +
                              std::chrono::milliseconds(std::clamp(grace_ms, 1000, 10000));

        while (proxy_.alive() && std::chrono::steady_clock::now() < deadline) {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }

        if (proxy_.alive()) {
            proxy_.terminate(1000);
        }
    }

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);

    do {
        try {
            auto p = proof();

            if (p.at("state") == "terminal" && p.at("alive") == false) {
                terminal_exit_ = p.at("exit_code").get<int>();

                return;
            }
        } catch (const std::exception &) { /* Ignored: an unreadable proof is polled again. */
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    } while (std::chrono::steady_clock::now() < deadline);

    throw VoyemuError("DIAGNOSTIC_STOP_UNVERIFIED", "native terminal proof unavailable");
}

int wine_native_proxy_main(int argc, char **argv) {
    try {
        need(argc == 3 && std::string(argv[1]) == "--request");
        auto request_file = std::filesystem::u8path(argv[2]);
        auto request = read_private_json(request_file, 1 << 20);
        need(request.at("schema") == 1);
        const auto launcher = request.at("launcher").get<std::string>();
        need(!launcher.empty() && launcher[0] == '/');

        return static_cast<int>(spawn({launcher, "--request", unix_path(request_file)}));
    } catch (const std::exception &) {
        return 64;
    }
}
} // namespace voyemu
