#include "lp_image.hpp"

#include <filesystem>
#include <iostream>

int main(int argc, char **argv) {
    if (argc != 3) {
        std::cerr << "Usage: voyemu_avd_layout SYSTEM_DISK INPUT_SHA256\n";

        return 2;
    }

    try {
        voyemu::Json layout;
        const auto disk = std::filesystem::absolute(argv[1]);

        for (const auto *name : {"system", "system_ext", "product", "vendor"}) {
            const auto partition = voyemu::read_lp_partition(disk, argv[2], name);
            layout["partitions"][name] = {{"offset", partition.offset}, {"bytes", partition.bytes}};
            layout["super_offset"] = partition.super_offset;
            layout["super_bytes"] = partition.super_bytes;
            layout["metadata_sha256"] = partition.metadata_sha256;
        }

        std::cout << layout.dump() << '\n';
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';

        return 1;
    }

    return 0;
}
