#include "model.hpp"
#include "process.hpp"
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#ifndef _WIN32
#include <cerrno>
#include <csignal>
#include <unistd.h>
#endif
using namespace voyemu;
namespace {
void need(bool value, const char *message) {
    if (!value) {
        throw std::runtime_error(message);
    }
}

std::string read(const std::filesystem::path &path) {
    std::ifstream file(path, std::ios::binary);

    need(bool(file), "runtime source open");

    return {(std::istreambuf_iterator<char>(file)), {}};
}

#ifndef _WIN32
const char *environment(const char *key) {
    auto value = std::getenv(key);

    need(value != nullptr, "recorder environment absent");

    return value;
}

std::string quote(std::string_view value) {
    std::string out = "'";

    for (char c : value) {
        if (c == '\'') {
            out += "'\\''";
        } else {
            out += c;
        }
    }

    return out + "'";
}

int record() {
    const int count = std::stoi(environment("VOYEMU_QBW_ARGC"));

    need(count > 0 && count <= 16, "recorder count");
    Json args = Json::array();

    for (int i = 0; i < 16; ++i) {
        char key[32];
        std::snprintf(key, sizeof key, "VOYEMU_QBW_ARG_%02d", i);
        auto value = std::getenv(key);

        if (i < count) {
            need(value, "recorder argument absent");
            args.push_back(value);
        } else {
            need(!value, "stale indexed argument");
        }
    }

    need(std::string(environment("VOYEMU_QBW_ENABLE")) == "1", "recorder mode");
    need(std::string(environment("LD_PRELOAD")) == "/proc/boot/bin/Snapdragon.rgb",
         "recorder preload");
    write_private_json(environment("QBW_TEST_RECEIPT"),
                       {{"argv", args},
                        {"pid", ::getpid()},
                        {"parent_pid", ::getppid()},
                        {"scope_parent_pid", std::stoi(environment("QBW_TEST_PARENT_PID"))}});

    return std::stoi(environment("QBW_TEST_STATUS"));
}
#endif
} // namespace

int main(int argc, char **argv) {
    try {
#ifdef _WIN32
        (void)argc;
        (void)argv;
        std::cout
                << "Mac Bash route test unavailable on Windows; actual QNX ksh acceptance is separate\n";

        return 77;
#else
        if (argc == 2 && std::string_view(argv[1]) == "--record") {
            return record();
        }

        need(argc == 2 || argc == 3, "usage project-root [runtime-own-source-fixture]");
        const auto root = std::filesystem::canonical(argv[1]),
                   self = std::filesystem::canonical(argv[0]);
        const auto source = read(argc == 3 ? std::filesystem::path(argv[2])
                                           : root / "src/guest/qnx/qnx-runtime.sh");
        const auto end = source.find("qm_wait_ram_driver() {");
        need(end != std::string::npos &&
                     source.find("qm_wait_ram_driver() {", end + 1) == std::string::npos,
             "unique runtime prefix");
        auto prefix = source.substr(0, end);
        const std::string
                old = "LD_PRELOAD=/proc/boot/bin/Snapdragon.rgb /proc/boot/bin/ksh -c 'exit 125'",
                updated =
                        "export LD_PRELOAD=/proc/boot/bin/Snapdragon.rgb\n    exec /proc/boot/bin/ksh -c 'exit 125'";
        const std::string carrier_header = "qm_native_external() (",
                          carrier_tail = "\n)\nqm_bounded() {";
        const auto carrier_begin = prefix.find(carrier_header),
                   carrier_end = prefix.find(carrier_tail, carrier_begin);
        need(carrier_begin != std::string::npos && carrier_end != std::string::npos &&
                     prefix.find(carrier_header, carrier_begin + 1) == std::string::npos,
             "unique external carrier function");
        auto at = prefix.find(updated, carrier_begin);
        const bool has_exec = at != std::string::npos && at < carrier_end;
        const auto marker = has_exec ? updated : old;

        if (!has_exec) {
            at = prefix.find(old, carrier_begin);
        }

        need(at != std::string::npos && at < carrier_end &&
                     (prefix.find(marker, at + 1) == std::string::npos ||
                      prefix.find(marker, at + 1) >= carrier_end),
             "unique external carrier command");
        const auto replacement =
                "export QBW_TEST_PARENT_PID=$$\n    " +
                std::string(has_exec ? "export LD_PRELOAD=/proc/boot/bin/Snapdragon.rgb\n    exec "
                                     : "LD_PRELOAD=/proc/boot/bin/Snapdragon.rgb ") +
                quote(self.string()) + " --record";
        prefix.replace(at, marker.size(), replacement);
        const auto work = root / ".local/build/tmp" / ("qnx-native-runtime-test-" + random_token());
        ensure_private_directory(work);
        const auto receipt = work / "record.json";
        struct Cleanup {
            std::filesystem::path path;

            ~Cleanup() {
                std::error_code error;
                std::filesystem::remove_all(path, error);
            }
        } cleanup{work};
        auto run = [&](const char *mode, const std::vector<std::string> &arguments, int code = 0,
                       std::string extra = {}) {
            std::filesystem::remove(receipt);
            ProcessSpec spec;
            spec.executable = "/bin/bash";
            spec.directory = root;
            std::string script = prefix + "\nunset VOYEMU_QBW_EXTERNAL\n";

            if (mode) {
                script += "export VOYEMU_QBW_EXTERNAL=" + quote(mode) + "\n";
            }

            script += extra + "\nqm_bounded route \"$@\"\n";
            spec.arguments = {"-c", script, "qnx-native-runtime"};
            spec.arguments.insert(spec.arguments.end(), arguments.begin(), arguments.end());
            spec.environment = {{"QBW_TEST_RECEIPT", receipt.string()},
                                {"QBW_TEST_STATUS", std::to_string(code)},
                                {"VOYEMU_QBW_ARG_15", "stale"}};
            auto result = execute(spec, 10000, 65536);
            need(!result.timed_out, "host route timed out");

            return result;
        };
        const std::vector<std::string> args = {"/usr/bin/command", "literal spaces $() ;\n", "",
                                               "\"quoted\""};

        for (int code : {0, 7, 124, 125, 127}) {
            auto result = run("1", args, code);

            if (result.exit_code != code) {
                throw std::runtime_error(
                        "native carrier status changed expected=" + std::to_string(code) +
                        " actual=" + std::to_string(result.exit_code) + ": " + result.output);
            }

            auto data = read_private_json(receipt, 4096);
            need(data.at("argv") == args, "literal argument transport changed");
            need(data.at("parent_pid") == data.at("scope_parent_pid"),
                 "carrier retained a redundant waiting shell instead of exec");
            errno = 0;
            need(::kill(data.at("pid").get<int>(), 0) < 0 && errno == ESRCH,
                 "recorder process remains");
            need(result.output.find("exit=route code=" + std::to_string(code)) != std::string::npos,
                 "outer status receipt absent");
        }

        {
            auto boundary = std::vector<std::string>(16, "literal");
            boundary[0] = "/usr/bin/command";
            boundary[15] = "last literal $() ;\n";
            auto result = run("1", boundary);
            need(result.exit_code == 0, "sixteen argument route rejected");
            auto data = read_private_json(receipt, 4096);
            need(data.at("argv") == boundary &&
                         data.at("parent_pid") == data.at("scope_parent_pid"),
                 "last indexed argument or exec parent changed");
        }

        for (const char *mode : {"", "0", "true", "11"}) {
            auto result = run(mode, {"/usr/bin/false"});
            need(result.exit_code == 125 && !std::filesystem::exists(receipt),
                 "invalid mode did not fail closed");
        }

        {
            auto result = run("1", std::vector<std::string>(17, "/bin/true"));
            need(result.exit_code == 125 && !std::filesystem::exists(receipt),
                 "argument count not bounded");
        }
        const auto function = "qm_test_function() { [ \"$1\" = literal ]; return 7; }";

        for (const char *mode : {static_cast<const char *>(nullptr), "1"}) {
            auto result = run(mode, {"qm_test_function", "literal"}, 0, function);
            need(result.exit_code == 7 && !std::filesystem::exists(receipt),
                 "legacy function route changed");
        }

        {
            auto result = run(nullptr, {"/usr/bin/false"});
            need(result.exit_code == 1 && !std::filesystem::exists(receipt),
                 "default external route changed");
        }

        if (argc == 2) {
            const std::string first = "echo 'QMC WAIT initial_sleep before'\n",
                              last = "echo 'QMC WAIT ram_driver after'\n";
            const auto begin = source.find(first), finish = source.find(last);
            need(begin != std::string::npos && finish != std::string::npos && begin < finish,
                 "actual startup wait trace boundaries");
            const auto body = source.substr(begin, finish + last.size() - begin);
            auto startup = [&](const std::string &mode, int initial, int driver) {
                ProcessSpec spec;
                spec.executable = "/bin/bash";
                spec.directory = root;
                spec.arguments = {"-c", "qm_initial_native_delay() { return " +
                                                std::to_string(initial) +
                                                "; }\nqm_wait_ram_driver() { return " +
                                                std::to_string(driver) + "; }\n" + body};

                if (!mode.empty()) {
                    spec.environment["VOYEMU_QNX_INITIAL_NATIVE_DELAY"] = mode;
                }

                return execute(spec, 5000, 4096);
            };

            for (const auto mode : {"", "1"}) {
                const auto result = startup(mode, 0, 0);
                need(result.exit_code == 0 && !result.timed_out, "startup wait success changed");
                std::size_t previous = 0;

                for (const auto marker :
                     {"QMC WAIT initial_sleep before", "QMC WAIT initial_sleep after",
                      "QMC WAIT ram_driver before", "QMC WAIT ram_driver after"}) {
                    const auto found = result.output.find(marker);
                    need(found != std::string::npos && found >= previous &&
                                 result.output.find(marker, found + 1) == std::string::npos,
                         "startup wait marker order/uniqueness");
                    previous = found;
                }
            }

            for (const auto mode : {"1", "invalid"}) {
                const auto result = startup(mode, 7, 0);
                need(result.exit_code == 125 &&
                             result.output.find("QMC WAIT initial_sleep after") ==
                                     std::string::npos &&
                             result.output.find("QMC WAIT ram_driver before") == std::string::npos,
                     "failed initial wait continued or changed exit");
            }

            const auto failed_driver = startup("1", 0, 1);
            need(failed_driver.exit_code == 1 &&
                         failed_driver.output.find("QMC WAIT ram_driver before") !=
                                 std::string::npos &&
                         failed_driver.output.find("QMC WAIT ram_driver after") ==
                                 std::string::npos,
                 "failed RAM driver wait continued or changed exit");
        }

        std::cout
                << "Mac actual shell prefix: exec direct scoped carrier parent, literal argv/status/env guards, absent/default legacy routes passed; no QNX ksh execution\n";

        return 0;
#endif
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';

        return 1;
    }
}
