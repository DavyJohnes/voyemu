#include "host_payload.hpp"
#include <fstream>
#include <iostream>
using namespace voyemu;

int main(int argc, char **argv) {
    try {
        if (argc == 3 && std::string_view(argv[1]) == "--inspect") {
            std::cout << bundled_frontend_record(std::filesystem::canonical(argv[2])).dump()
                      << '\n';

            return 0;
        }

        const auto root = std::filesystem::canonical(std::filesystem::temp_directory_path()) /
                          ("voyemu-host-payload-" + random_token());
        ensure_private_directory(root);
        struct Cleanup {
            std::filesystem::path path;

            ~Cleanup() {
                std::filesystem::remove_all(path);
            }
        } cleanup{root};
        auto reject = [&] {
            try {
                (void)bundled_frontend_record(root);
            } catch (const std::exception &) {
                return;
            }

            throw std::runtime_error("invalid root program accepted");
        };
        reject();
        const auto program = host_program(root);
        std::ofstream(program) << "own program and embedded resources";
        const auto record = bundled_frontend_record(root);

        if (record.at("source_root") != root.string() ||
            record.at("executable") != program.string() || record.at("pins").size() != 1 ||
            record.at("pins")[0].at("sha256") != sha256_file(program)) {
            throw std::runtime_error("root program relocation failed");
        }

        std::filesystem::create_hard_link(program, root / "linked-program");
        reject();
        std::filesystem::remove(root / "linked-program");
        std::filesystem::rename(program, root / "saved-program");
        std::filesystem::create_symlink(root / "saved-program", program);
        reject();
        std::cout << "single root program relocation and ordinary-file guards passed\n";

        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';

        return 1;
    }
}
