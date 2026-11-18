#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <unistd.h>
#include <vector>

int main(int argc, char **argv) {
    const auto binary = std::getenv("VOYEMU_QNX_CAPTURE_GRAPHICS"),
               journal = std::getenv("VOYEMU_QNX_CAPTURE_JOURNAL");

    if (argc < 2 || !binary || !journal) {
        return 2;
    }

    const std::filesystem::path executable = binary, directory = journal;

    if (!executable.is_absolute() || executable.lexically_normal() != executable ||
        !directory.is_absolute() || directory.lexically_normal() != directory ||
        std::filesystem::is_symlink(executable) || std::filesystem::is_symlink(directory) ||
        !std::filesystem::is_regular_file(executable) ||
        !std::filesystem::is_directory(directory)) {
        return 2;
    }

    std::vector<char *> args{const_cast<char *>(binary), const_cast<char *>("--checkpoint-record"),
                             const_cast<char *>(journal)};

    for (int i = 1; i < argc; i++) {
        args.push_back(argv[i]);
    }

    args.push_back(nullptr);
    execv(binary, args.data());
    std::perror("owned graphics record exec");

    return 1;
}
