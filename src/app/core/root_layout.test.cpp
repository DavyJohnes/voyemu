#include "model.hpp"
#include "process.hpp"
#include <iostream>
using namespace voyemu;

static void check(bool value, const char *message) {
    if (!value) {
        throw std::runtime_error(message);
    }
}

int main(int argc, char **argv) {
    try {
        check(argc == 2, "native CLI path required");
        const auto executable = std::filesystem::canonical(argv[1]);
        auto project = executable.parent_path();

        while (!project.empty() && !std::filesystem::is_directory(project / "src/app")) {
            if (project == project.parent_path()) {
                break;
            }

            project = project.parent_path();
        }

        check(std::filesystem::is_directory(project / "src/app"), "project root required");
        const auto root = project / ".local/build/tmp" / ("voyemu-root-layout-" + random_token());
        ensure_private_directory(root / "unrelated/.local/machines");
        write_private_json(root / "unrelated/profiles/working-directory-only/profile.json",
                           {{"schema", 1}});
        struct Cleanup {
            std::filesystem::path path;

            ~Cleanup() {
                std::filesystem::remove_all(path);
            }
        } cleanup{root};
        auto inspect = [&](const std::filesystem::path &binary) {
            ProcessSpec command;
            command.executable = binary;
            command.arguments = {"list", "--lang=en"};
            command.directory = root / "unrelated";
            command.output = root / "list.log";
            const auto result = execute(command, 10000);
#ifdef _WIN32
            // Console text output ends lines with CRLF on Windows.
            const std::string heading = "Profiles\r\n";
#else
            const std::string heading = "Profiles\n";
#endif
            check(result.exit_code == 0 && result.output.starts_with(heading),
                  "read-only list returns human profile names");

            return result.output;
        };
        check(inspect(executable).find("working-directory-only") == std::string::npos,
              "executable uses its own project rather than another project's working directory");
        const auto runtime = root / "runtime";
        ensure_private_directory(runtime / ".local/machines");
        write_private_json(runtime / "profiles/runtime-only/profile.json", {{"schema", 1}});
#ifdef _WIN32
        const auto copied = runtime / "voyemu.exe";
#else
        const auto copied = runtime / "voyemu";
#endif
        check(std::filesystem::copy_file(executable, copied), "owned runtime fixture copy");
        check(inspect(copied).find("runtime-only") != std::string::npos,
              "ready runtime root does not need the native source tree");
        check(!std::filesystem::exists(runtime / ".local/machines/sessions") &&
                      !std::filesystem::exists(root / "unrelated/.local/machines/sessions"),
              "root inspection never creates sessions");
        std::cout << "project-root CLI and standalone runtime root detection passed\n";

        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';

        return 1;
    }
}
