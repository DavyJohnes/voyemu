#pragma once
#include <filesystem>
#include <string>
#include <vector>
namespace voyemu {
int graphics_main(int argc, char **argv);
int window_main(int argc, char **argv);

inline bool builtin_program_name(const std::filesystem::path &program) {
    const auto name = program.filename().string();

    return name == "voyemu" || name == "voyemu.exe";
}

inline void program_mode(const std::filesystem::path &program, const char *mode,
                         std::vector<std::string> &arguments, bool builtin = false) {
    if (builtin || builtin_program_name(program)) {
        arguments.insert(arguments.begin(), mode);
    }
}
} // namespace voyemu
