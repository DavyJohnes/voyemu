#include "passion_owned_watch.hpp"
#include <cassert>
#include <iostream>
#include <thread>
#ifdef _WIN32
#include <exception>
#include <fcntl.h>
#include <io.h>
#endif
using namespace voyemu;
using namespace voyemu::media;

int main(int argc, char **argv) try {
    if (argc == 3 && std::string(argv[1]) == "--emit") {
#ifdef _WIN32
        // The guest log bytes pass through unchanged; Windows text mode would add CR.
        _setmode(_fileno(stdout), _O_BINARY);
#endif
        std::ifstream in(argv[2], std::ios::binary);

        if (!in || std::filesystem::file_size(argv[2]) > 100000) {
            return 2;
        }

        std::cout << in.rdbuf();

        return std::cout ? 0 : 3;
    }

    auto base = argc == 2 ? std::filesystem::path(argv[1])
                          : std::filesystem::current_path() / ".local/build/tmp";
    ensure_private_directory(base);
    auto root = base / ("passion-owned-watch-" + random_token());
    ensure_private_directory(root);
    auto executable = std::filesystem::canonical(argv[0]);
    auto directory = root / "logs";
    ensure_private_directory(directory);
    auto input = root / "input.txt";
    {
        std::ofstream f(input, std::ios::binary);
        f << "{\"owned\":true}\n";
    }

    for (unsigned i = 0; i < 12; i++) {
        ProcessSpec spec;
        spec.executable = executable;
        spec.arguments = {"--emit", input.string()};
        spec.output = directory / (std::to_string(i) + ".jsonl");
        auto child = passion_owned_watch(spec, directory);
        auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(1);

        while (child.alive() && std::chrono::steady_clock::now() < deadline) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }

        assert(!child.alive());
        assert(child.read(65536) == "{\"owned\":true}\n");

        if (i >= 9) {
            child.preserve();
        }

        child.stop();
        assert(!std::filesystem::exists(spec.output));
        std::size_t files = 0;
        std::uint64_t bytes = 0;

        for (auto &entry : std::filesystem::directory_iterator(directory)) {
            files++;
            bytes += entry.file_size();
        }

        assert(files <= 3 && bytes < 10000);
    }

    {
        std::ofstream f(input);
        f << std::string(100000, 'x');
    }
    ProcessSpec spec;
    spec.executable = executable;
    spec.arguments = {"--emit", input.string()};
    spec.output = directory / "oversized.jsonl";
    auto child = passion_owned_watch(spec, directory);
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(1);

    while (child.alive() && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    assert(!child.alive());
    child.preserve();
    child.stop();
    assert(std::filesystem::file_size(directory / "last-failed-watch.jsonl") == 65536);
    auto receipt = read_private_json(directory / "last-failure.json");
    assert(receipt.at("original_bytes") == 100000 && receipt.at("bytes") == 65536 &&
           receipt.at("sha256_scope") == "retained prefix");
    std::cout
            << "PASS13 retained own children terminal;12 rotations leave at most3 bounded files; oversize preserved prefix64KiB+receipt; fixture removed\n";
    std::filesystem::remove_all(root);
} catch (const std::exception &error) {
    std::cerr << error.what() << '\n';

    return 1;
}
