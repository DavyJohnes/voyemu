#include "model.hpp"
#include "process.hpp"
#include "zip_archive.hpp"
#include <fstream>
#include <iostream>
using namespace voyemu;

int main(int argc, char **argv) {
    try {
        if (argc == 4 && std::string(argv[1]) == "--jar") {
            const auto entries = pinned_zip_inventory(std::filesystem::absolute(argv[2]), argv[3]);
            bool preparation = false, capture = false;

            for (const auto &e : entries) {
                preparation |= e.name == "com/voyemu/prepare/PreparationMain.class";
                capture |= e.name == "com/voyemu/prepare/VehicleCaptureMain.class";
            }

            if (!preparation || !capture) {
                throw std::runtime_error("current helper classes absent");
            }

            std::cout << "current Java helper pinned ZIP inventory passed\n";

            return 0;
        }

        const std::string hex =
                "504b030414000000080083314c5d12e573450e0000000c00000013000000416e64726f69644d616e69666573742e786d6ccb4b2cc92c4b65a8ca2cf8cf0500504b030414000008080083314c5d03ea60490d0000000b000000170000006173736574732fd0b4d0b0d0bdd0bdd18bd0b52e747874cb2fcf5348cbac28292d4a0500504b0102140314000000080083314c5d12e573450e0000000c000000130000000000000000000000800100000000416e64726f69644d616e69666573742e786d6c504b0102140314000008080083314c5d03ea60490d0000000b00000017000000000000000000000080013f0000006173736574732fd0b4d0b0d0bdd0bdd18bd0b52e747874504b0506000000000200020086000000810000000000";
        std::string bytes;

        for (std::size_t i = 0; i < hex.size(); i += 2) {
            bytes += static_cast<char>(std::stoul(hex.substr(i, 2), nullptr, 16));
        }

        const auto dir = std::filesystem::canonical(std::filesystem::temp_directory_path()) /
                         ("zip-inventory-" + random_token());
        ensure_private_directory(dir);
        struct Cleanup {
            std::filesystem::path p;

            ~Cleanup() {
                std::error_code e;
                std::filesystem::remove_all(p, e);
            }
        } cleanup{dir};
        const auto archive = dir / "fixture.zip";
        {
            std::ofstream out(archive, std::ios::binary);
            out << bytes;
        }
        const auto entries = pinned_zip_inventory(archive, sha256_bytes(bytes));

        if (entries.size() != 2 || entries[0].name != "AndroidManifest.xml" ||
            entries[0].bytes != 12 || entries[1].bytes != 11) {
            throw std::runtime_error("portable inventory changed names/extents");
        }

        bool failed = false;
        try {
            pinned_zip_inventory(archive, std::string(64, '0'));
        } catch (...) {
            failed = true;
        }

        if (!failed) {
            throw std::runtime_error("wrong archive SHA accepted");
        }

        bytes[30] = 'X';
        {
            std::ofstream out(archive, std::ios::binary | std::ios::trunc);
            out << bytes;
        }
        failed = false;
        try {
            pinned_zip_inventory(archive, sha256_bytes(bytes));
        } catch (...) {
            failed = true;
        }

        if (!failed) {
            throw std::runtime_error("local/central header mismatch accepted");
        }

        std::cout << "portable pinned ZIP inventory, Unicode, SHA and structural guards passed\n";

        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';

        return 1;
    }
}
