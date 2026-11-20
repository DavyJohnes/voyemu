#include "model.hpp"
#include "process.hpp"
#include <chrono>
#include <fstream>
#include <iostream>
#include <regex>
#include <thread>
#ifndef _WIN32
#include <csignal>
#endif
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
        // A failed command names its log file in the installation, which keeps the English text
        // of the error as JSON Lines.
        const auto named_log = [&](const std::string &output) {
            std::smatch match;
            require(std::regex_search(
                            output, match,
                            std::regex(R"((?:Log|Details are in the log): ([^\n]+?\.log))")),
                    "failed command named no log: " + output);
            const std::filesystem::path log = match[1].str();
            require(log.parent_path() == root / ".local/build/logs" &&
                            std::filesystem::is_regular_file(log),
                    "named log is not a file of .local/build/logs: " + log.string());
            std::ifstream stream(log);

            return std::string(std::istreambuf_iterator<char>(stream), {});
        };
        // Log records look like "component: message"; the console of a command shows none.
        const auto shows_records = [](const std::string &output) {
            return std::regex_search(output, std::regex(R"((^|\n)[a-z][a-z_]*: )"));
        };
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

#ifndef _WIN32
        // Ctrl+C stops fetch while it hashes the update archive and ends it with code 130.
        {
            // A synthetic profile whose owner archive the import hashes for seconds.
            const std::string synthetic = "sigint-fixture";
            const auto archive = root / "update.zip";
            const std::string pin(64, '0');
            write_private_json(
                    root / "profiles" / synthetic / "profile.json",
                    {{"schema", 1},
                     {"acquisition", {{"format", "pinned-zip-tgz-raw-scatter"}, {"diff", false}}},
                     {"ownerUpdate",
                      {{"file", "update.zip"},
                       {"bytes", 4ULL << 30},
                       {"sha256", pin},
                       {"inner", {{"file", "inner.tgz"}, {"sha256", pin}}}}}});
            write_private_json(root / "profiles" / synthetic / "profile-qnx.json",
                               {{"schema", 1},
                                {"update",
                                 {{"container", "zip-stored-tgz"},
                                  {"sha256", pin},
                                  {"nested_member", "inner.tgz"},
                                  {"nested_sha256", pin}}}});
            std::ofstream(archive).close();
            std::filesystem::resize_file(archive, 4ULL << 30);
            ProcessSpec spec;
            spec.executable = installed;
            spec.arguments = {"fetch", synthetic, "--source=" + archive.string(), "--lang=en"};
            spec.directory = root;
            spec.output = root / "fetch-interrupted.log";
            auto fetch = ChildProcess::launch(spec);
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            const auto signalled = std::chrono::steady_clock::now();
            require(::kill(static_cast<pid_t>(fetch.pid()), SIGINT) == 0, "SIGINT not sent");

            while (fetch.alive() &&
                   std::chrono::steady_clock::now() - signalled < std::chrono::seconds(30)) {
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
            }

            const auto stopped = std::chrono::steady_clock::now() - signalled;
            std::ifstream log(spec.output);
            const std::string output(std::istreambuf_iterator<char>(log), {});
            require(!fetch.alive() && fetch.exit_code() == 130 &&
                            stopped < std::chrono::seconds(3) &&
                            output.find(
                                    "Voyemu: INTERRUPTED: the command was interrupted.\nLog: ") !=
                                    std::string::npos,
                    "Ctrl+C did not stop fetch promptly with code 130: " +
                            std::to_string(fetch.exit_code()) + " " + output);
            require(named_log(output).find("\"command.started\"") != std::string::npos,
                    "interrupted fetch log lacks its start record");
            std::filesystem::remove(archive);
        }
#endif

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
                    require(result.output.starts_with("Voyemu: DOWNLOAD_OFFLINE: ") &&
                                    result.output.find(
                                            " is not downloaded and --offline forbids downloads.") !=
                                            std::string::npos,
                            "Android-only build did not reach observer preparation with absent fixture toolchain sources: " +
                                    result.output);
                    require(!shows_records(result.output),
                            "build printed log records without --verbose: " + result.output);
                    // --verbose prints the records of the log on the console as well.
                    arguments.emplace_back("--verbose");
                    const auto verbose = run(arguments);
                    require(verbose.exit_code == result.exit_code &&
                                    verbose.output.find("cli: command.started; command=build") !=
                                            std::string::npos &&
                                    verbose.output.find(
                                            "cli: DOWNLOAD_OFFLINE: pinned cache file missing") !=
                                            std::string::npos,
                            "build --verbose did not print its log records: " + verbose.output);
                }

                require(sha256_file(file) == pin &&
                                !std::filesystem::exists(root / "machines" / profile) &&
                                std::filesystem::is_directory(root / ".local/machines") &&
                                std::filesystem::is_empty(root / ".local/machines"),
                        "failed offline preparation changed status, published a machine or started a session");
            }
        }

        // Every ordinary command of an installation keeps a log, list included.
        {
            ProcessSpec spec;
            spec.executable = installed;
            spec.arguments = {"list", "--lang=en"};
            spec.directory = root;
            const auto listed = execute(spec, 15000);
            bool logged = false;

            for (const auto &entry :
                 std::filesystem::directory_iterator(root / ".local/build/logs")) {
                if (entry.path().filename().string().starts_with("list-")) {
                    std::ifstream stream(entry.path());
                    logged = std::string(std::istreambuf_iterator<char>(stream), {})
                                     .find("\"command.started\"") != std::string::npos;
                }
            }

            require(!listed.timed_out && listed.exit_code == 0 && logged &&
                            !shows_records(listed.output),
                    "list kept no log or printed records: " + listed.output);
        }

        // JSON automation keeps its single JSON reply and writes no command log.
        {
            ProcessSpec spec;
            spec.executable = installed;
            spec.arguments = {"test", "--request", (root / "missing-request.json").string()};
            spec.directory = root;
            const auto reply = execute(spec, 15000);
            const auto parsed = Json::parse(reply.output, nullptr, false);
            require(!reply.timed_out && reply.exit_code == 4 && parsed.is_object() &&
                            parsed.at("ok") == false,
                    "test did not keep its JSON reply: " + reply.output);

            for (const auto &entry :
                 std::filesystem::directory_iterator(root / ".local/build/logs")) {
                require(!entry.path().filename().string().starts_with("test-"),
                        "test wrote a command log");
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
