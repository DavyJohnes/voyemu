#include "model.hpp"
#include "process.hpp"
#include <iostream>
using namespace voyemu;

static void require(bool value, const std::string &message) {
    if (!value) {
        throw std::runtime_error(message);
    }
}

int main(int argc, char **argv) {
    try {
        require(argc == 2, "CLI binary required");
        const auto project = std::filesystem::canonical(std::filesystem::path(__FILE__))
                                     .parent_path()
                                     .parent_path()
                                     .parent_path()
                                     .parent_path();
        const auto root = project / ".local/build/tmp" / ("public-build-cli-" + random_token());
        ensure_private_directory(root / ".local/machines");
        struct Cleanup {
            std::filesystem::path root;

            ~Cleanup() {
                std::filesystem::remove_all(root);
            }
        } cleanup{root};
#ifdef _WIN32
        const auto installed = root / "voyemu.exe";
#else
        const auto installed = root / "voyemu";
#endif
        std::filesystem::copy_file(std::filesystem::canonical(argv[1]), installed);
        const auto run_unknown = [&](const char *command) {
            ProcessSpec spec;
            spec.executable = installed;
            spec.arguments = {command, "nosuch", "--offline", "--lang=en"};
            spec.directory = root;

            return execute(spec, 15000);
        };

        // An unknown profile stops deps and build before any work, as it stops fetch.
        for (const auto *command : {"deps", "fetch", "build"}) {
            const auto unknown = run_unknown(command);
            require(!unknown.timed_out && unknown.exit_code == 4 &&
                            unknown.output.starts_with("Voyemu: PROFILE_NOT_FOUND:"),
                    std::string(command) + " accepted an unknown profile: " + unknown.output);
        }

        for (const auto *profile :
             {"free-11-chinese-20260731-7.3.1", "passion-11-russian-20260403-2.1.4.80"}) {
            const auto file = root / "profiles" / profile / "profile.json";
            const auto definition =
                    read_private_json(project / "profiles" / profile / "profile.json");
            require(definition.at("status") == "ready", "API30 source acceptance scope differs");
            write_private_json(file, definition);
            const auto pin = sha256_file(file);
            auto run = [&](std::vector<std::string> arguments) {
                ProcessSpec command;
                command.executable = installed;
                command.arguments = std::move(arguments);
                // Human text assertions read English regardless of the host locale.
                command.arguments.emplace_back("--lang=en");
                command.directory = root;

                return execute(command, 15000);
            };
            const auto disabled =
                    run({"build", profile, "--cluster=off", "--multimedia=off", "--offline"});
            require(disabled.exit_code != 0 &&
                            disabled.output.find("cannot both be off") != std::string::npos,
                    "both-off build was not rejected before preparation: " + disabled.output);

            for (const bool android_only : {true, false}) {
                std::vector<std::string> arguments{"build", profile, "--offline"};

                if (android_only) {
                    arguments.insert(arguments.end(), {"--cluster=off", "--multimedia=on"});
                }

                const auto result = run(arguments);
                require(!result.timed_out && result.exit_code != 0,
                        "fixture without retained inputs unexpectedly built or timed out");
                require(result.output.starts_with("Voyemu:") &&
                                result.output.find("CLI_ARGUMENT:") == std::string::npos &&
                                result.output.find("PROFILE_INCOMPLETE:") == std::string::npos,
                        "public API30 build did not reach genuine preparation: " + result.output);

                // Observer preparation first needs the Java runtime, which offline is not cached.
                if (android_only) {
                    require(result.output.starts_with(
                                    "Voyemu: DOWNLOAD_OFFLINE: pinned cache file missing"),
                            "Android-only build did not reach observer preparation with absent fixture toolchain sources: " +
                                    result.output);
                }

                require(sha256_file(file) == pin &&
                                !std::filesystem::exists(root / "machines" / profile) &&
                                std::filesystem::is_directory(root / ".local/machines") &&
                                std::filesystem::is_empty(root / ".local/machines"),
                        "failed offline preparation changed status, published a machine or started a session");
            }
        }

        // The preparation commands recreate the missing roots of .local as private directories.
        for (const auto *name : {"profiles", "toolchains", "build", "machines"}) {
            const auto directory = root / ".local" / name;
            require(std::filesystem::is_directory(directory) &&
                            !std::filesystem::is_symlink(directory),
                    std::string("preparation did not recreate .local/") + name);
#ifndef _WIN32
            require(std::filesystem::status(directory).permissions() ==
                            std::filesystem::perms::owner_all,
                    std::string(".local/") + name + " is not private");
#endif
        }

        std::cout
                << "Public Free/Passion API30 build roles reach genuine offline preparation without false readiness or guests\n";

        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';

        return 1;
    }
}
