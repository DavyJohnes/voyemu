#include "control_artwork.hpp"
#include <iostream>
#include <stdexcept>
using voyemu::cluster::control_artwork_directory;

int main() {
    try {
        const std::filesystem::path root = "/prepared-parent/profiles/project";
        const auto first = root / "machines/first/ui/wheel",
                   second = root / "machines/second/ui/wheel";

        if (control_artwork_directory(first, "shared-profile") !=
            first.parent_path() / "controls") {
            throw std::runtime_error("first machine controls escaped selected machine");
        }

        if (control_artwork_directory(second, "shared-profile") !=
            second.parent_path() / "controls") {
            throw std::runtime_error("second machine controls escaped selected machine");
        }

        if (control_artwork_directory(first, "shared-profile") ==
            control_artwork_directory(second, "shared-profile")) {
            throw std::runtime_error("named machines share controls");
        }

        const auto source = root / "profiles/common/wheels/free-2024";

        if (control_artwork_directory(source, "developer-profile") !=
            root / "machines/developer-profile/ui/controls") {
            throw std::runtime_error("developer source fallback changed");
        }

        if (control_artwork_directory("/custom/assets", "fixture") != "/custom/assets/controls") {
            throw std::runtime_error("custom asset fallback changed");
        }

        std::cout
                << "machine-local controls, named-machine isolation and developer fallback passed\n";

        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';

        return 1;
    }
}
