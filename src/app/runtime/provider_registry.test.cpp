#include "provider_registry.hpp"
#include "provider_process.hpp"
#include <cassert>
#include <fstream>
#include <iostream>
#ifdef _WIN32
#include "../core/private_state_security.hpp"
#else
#include <sys/stat.h>
#include <unistd.h>
#endif
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

void public_result(const std::filesystem::path &path, const Json &reply) {
#ifdef _WIN32
    voyemu::detail::PrivateStateSecurity security;
    BYTE world[SECURITY_MAX_SID_SIZE]{};
    DWORD size = sizeof world;
    assert(CreateWellKnownSid(WinWorldSid, nullptr, world, &size));
    EXPLICIT_ACCESSW entry{};
    entry.grfAccessPermissions = FILE_GENERIC_READ;
    entry.grfAccessMode = GRANT_ACCESS;
    entry.Trustee.TrusteeForm = TRUSTEE_IS_SID;
    entry.Trustee.ptstrName = reinterpret_cast<LPWSTR>(world);
    PACL acl = nullptr;
    assert(SetEntriesInAclW(1, &entry, security.acl, &acl) == ERROR_SUCCESS);
    assert(SetSecurityDescriptorDacl(&security.descriptor, TRUE, acl, FALSE));
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE | READ_CONTROL, 0, &security.attributes,
                              CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    assert(file != INVALID_HANDLE_VALUE);
    PACL observed = nullptr;
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    assert(GetSecurityInfo(file, SE_FILE_OBJECT, DACL_SECURITY_INFORMATION, nullptr, nullptr,
                           &observed, nullptr, &descriptor) == ERROR_SUCCESS);
    bool found = false;

    for (DWORD i = 0; observed && i < observed->AceCount; ++i) {
        void *raw = nullptr;

        if (GetAce(observed, i, &raw) &&
            static_cast<ACE_HEADER *>(raw)->AceType == ACCESS_ALLOWED_ACE_TYPE) {
            auto ace = static_cast<ACCESS_ALLOWED_ACE *>(raw);

            if (EqualSid(&ace->SidStart, world) &&
                (ace->Mask & FILE_GENERIC_READ) == FILE_GENERIC_READ) {
                found = true;
            }
        }
    }

    assert(found);

    if (descriptor) {
        LocalFree(descriptor);
    }

    const auto bytes = reply.dump();
    DWORD count = 0;
    assert(WriteFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &count, nullptr) &&
           count == bytes.size());
    CloseHandle(file);
    LocalFree(acl);
#else
    {
        std::ofstream file(path);
        file << reply.dump();
    }
    assert(chmod(path.c_str(), 0644) == 0);
#endif
}
} // namespace

int main(int argc, char **argv) {
    try {
#ifdef _WIN32
        if (argc == 6 && std::string(argv[2]) == "--request" &&
            std::string(argv[4]) == "--response") {
            const auto payload = read_private_json(std::filesystem::u8path(argv[3]), 16384);
            write_private_json(std::filesystem::u8path(argv[5]), response(payload));

            return 0;
        }
#endif
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
#ifndef _WIN32
            assert(std::string(error.what()) == "PROVIDER_JOBS: provider job limit reached");
#endif
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
#ifdef _WIN32
        auto actual = manifest;
        const auto executable = root / "runtime.exe", replacement = root / "replacement.exe";
        {
            voyemu::detail::PrivateStateSecurity security;
            HANDLE owned = CreateFileW(executable.c_str(), GENERIC_WRITE, 0, &security.attributes,
                                       CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
            assert(owned != INVALID_HANDLE_VALUE && CloseHandle(owned));
            std::ifstream source(std::filesystem::canonical(argv[0]), std::ios::binary);
            std::ofstream target(executable, std::ios::binary);
            target << source.rdbuf();
            assert(source && target);
        }
        std::filesystem::copy_file(executable, replacement);
        actual["runtime"] = {{"path", executable.string()}, {"sha256", sha256_file(executable)}};
        ProviderRegistry live(jobs);
        live.load(actual);
        const auto pinned_runner = [&](const ProcessSpec &spec, int milliseconds) {
            HANDLE write = CreateFileW(executable.c_str(), GENERIC_WRITE,
                                       FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                       nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);

            if (write != INVALID_HANDLE_VALUE) {
                CloseHandle(write);
            }

            assert(write == INVALID_HANDLE_VALUE && GetLastError() == ERROR_SHARING_VIOLATION);
            assert(!MoveFileExW(replacement.c_str(), executable.c_str(),
                                MOVEFILE_REPLACE_EXISTING));
            assert(GetLastError() == ERROR_SHARING_VIOLATION ||
                   GetLastError() == ERROR_ACCESS_DENIED);

            return run_provider(spec, milliseconds);
        };
        const auto observed = live.collect("example", context, 5000, pinned_runner, [] {
            return true;
        });
        assert(observed["status"] == "ready" && observed["data"]["value"] == 0 &&
               observed["boot_id"] == "boot" && observed["request_id"] == "request");
        check_clean();
        assert(sha256_file(executable) == actual["runtime"]["sha256"].get<std::string>());
        HANDLE released = CreateFileW(executable.c_str(), GENERIC_WRITE,
                                      FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                      nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        assert(released != INVALID_HANDLE_VALUE);
        CloseHandle(released);
        std::cout
                << "PASS Windows pinned manifest to real self-worker collect, filesystem guards and result/fence negatives\n";
#else
        std::cout << "PASS provider registry guards and result/fence negatives\n";
#endif

        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';

        return 1;
    }
}
