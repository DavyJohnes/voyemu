#include "qnx_gpt.hpp"
#include "error.hpp"
#include "process.hpp"
#include "quickboot.hpp"
#include <fstream>
#include <iostream>
#include <limits>
using namespace voyemu;

void loader_segments_fixture() {
    auto root = std::filesystem::current_path();

    for (unsigned i = 0;
         i < 8 && !std::filesystem::is_regular_file(root / "src/app/CMakeLists.txt"); ++i) {
        root = root.parent_path();
    }

    if (!std::filesystem::is_regular_file(root / "src/app/CMakeLists.txt")) {
        throw std::runtime_error("segment fixture needs project directory");
    }

    const auto scratch = root / ".local/build/tmp" / ("qnx-loader-segment-test-" + random_token());
    ensure_private_directory(scratch.parent_path());

    if (!std::filesystem::create_directory(scratch)) {
        throw std::runtime_error("fresh segment fixture");
    }

    ensure_private_directory(scratch);
    try {
        const auto input = scratch / "input.img";
        const std::string payload = "0123456789abcdefXYZ";
        {
            std::ofstream f(input, std::ios::binary);
            f << payload;
        }
        const auto pin = sha256_bytes(payload);
        auto rejects = [&](auto action) {
            bool failed = false;
            try {
                action();
            } catch (...) {
                failed = true;
            }

            if (!failed) {
                throw std::runtime_error("segment negative guard accepted");
            }
        };
        rejects([&] {
            split_qnx_loader_image(input, std::string(64, '0'), scratch / "bad-sha", 8);
        });
        rejects([&] {
            split_qnx_loader_image(input, pin, scratch / "zero-cap", 0);
        });
        rejects([&] {
            split_qnx_loader_image(input, pin, scratch / "large-cap", (1ULL << 30) + 1);
        });
        rejects([&] {
            split_qnx_loader_image(input, pin, "relative", 8);
        });
        bool reserve_bound = false;
        try {
            split_qnx_loader_image(input, pin, scratch / "reserve-bound", 8, (20ULL << 30) + 1);
        } catch (const VoyemuError &error) {
            reserve_bound = error.code() == "QNX_LOADER_SEGMENT_RESERVE_BOUND";
        }

        if (!reserve_bound || std::filesystem::exists(scratch / "reserve-bound")) {
            throw std::runtime_error("segment reserve bound/output");
        }

        auto reserve_result = [&](const auto &target, bool explicit_reserve,
                                  std::uint64_t reserve = 20ULL << 30) {
            try {
                if (explicit_reserve) {
                    split_qnx_loader_image(input, pin, target, 8, reserve);
                } else {
                    split_qnx_loader_image(input, pin, target, 8);
                }

                return std::string{};
            } catch (const VoyemuError &error) {
                return std::string(error.code());
            }
        };
        const auto default_result = reserve_result(scratch / "default-reserve", false),
                   explicit_result = reserve_result(scratch / "explicit-reserve", true);

        if (default_result != explicit_result ||
            (!default_result.empty() && default_result != "QNX_LOADER_SEGMENT_RESERVE")) {
            throw std::runtime_error("segment default production reserve differs");
        }

        const auto maximum_result = reserve_result(scratch / "maximum-reserve", true,
                                                   std::numeric_limits<std::uint64_t>::max());

        if (maximum_result != "QNX_LOADER_SEGMENT_RESERVE_BOUND" ||
            std::filesystem::exists(scratch / "maximum-reserve")) {
            throw std::runtime_error("segment maximum reserve typed guard/output");
        }

        const auto parts = split_qnx_loader_image(input, pin, scratch / "parts", 8, 0);

        if (parts.size() != 3) {
            throw std::runtime_error("segment count");
        }

        std::string joined;

        for (std::size_t i = 0; i < parts.size(); ++i) {
            const auto &p = parts[i];
            std::ifstream f(p.file, std::ios::binary);
            const std::string b((std::istreambuf_iterator<char>(f)), {});

            if (p.offset != i * 8 || p.bytes != (i < 2 ? 8 : 3) ||
                b != payload.substr(i * 8, p.bytes) || sha256_bytes(b) != p.sha256) {
                throw std::runtime_error("segment coverage/hash/readback");
            }

            joined += b;
        }

        if (joined != payload || sha256_bytes(joined) != pin || sha256_file(input) != pin) {
            throw std::runtime_error("segment whole digest/source");
        }

        rejects([&] {
            split_qnx_loader_image(input, pin, scratch / "parts", 8, 0);
        });
        std::filesystem::remove_all(scratch);
    } catch (...) {
        std::filesystem::remove_all(scratch);
        throw;
    }
}

namespace {
std::string qinggan11_read(const char *path) {
    std::ifstream file(path, std::ios::binary);

    if (!file) {
        throw std::runtime_error("Free A11 GPT fixture open");
    }

    return std::string((std::istreambuf_iterator<char>(file)), {});
}

template <class Function> void qinggan11_rejects(Function action) {
    bool rejected = false;

    try {
        action();
    } catch (...) {
        rejected = true;
    }

    if (!rejected) {
        throw std::runtime_error("Free A11 GPT negative accepted");
    }
}

void qinggan11_gpt_fixture(const char *path) {
    const auto image = qinggan11_read(path), pin = sha256_bytes(image);
    const auto images = qnx_qinggan11_gpt(image, pin);

    validate_qnx_qinggan11_gpt(images, image, pin);
    const auto loader = qnx_qinggan11_loader_image(image, pin);

    if (loader.size() != 147841024 || loader.size() % 4096 != 0 ||
        loader != image.substr(0, loader.size()) ||
        0xa0000000ULL + loader.size() >= 0xaffc0000ULL ||
        0xb0000000ULL + loader.size() >= 0xbffbe000ULL) {
        throw std::runtime_error("Free A11 loader prefix/ROM extent");
    }

    if (pin == "8b7f821d339af539aed83ff50b940e0f647eb95f37ec2c71a8b3fff2fdc485f2" &&
        sha256_bytes(loader) !=
                "b952c6730dbd5d0bc6abc97ccb605c54c3e1222485e7bc81518bcea77dcf6ecc") {
        throw std::runtime_error("Free A11 accepted loader prefix SHA");
    }

    qinggan11_rejects([&] {
        qnx_qinggan11_loader_image(image, std::string(64, '0'));
    });
    qinggan11_rejects([&] {
        const auto short_image = image.substr(0, image.size() - 1);
        qnx_qinggan11_loader_image(short_image, sha256_bytes(short_image));
    });
    qinggan11_rejects([&] {
        auto tail = image;
        tail[147839040] = 0;
        qnx_qinggan11_loader_image(tail, sha256_bytes(tail));
    });
    qinggan11_rejects([&] {
        auto tail = image;
        tail.back() = 0;
        qnx_qinggan11_loader_image(tail, sha256_bytes(tail));
    });

    if (sha256_bytes(images.cache) !=
                "1b5b6d8cda141e4c8294f096b42538f3dddfec5c18461395d5363d5dc512379f" ||
        sha256_bytes(images.primary) !=
                "d3829189267eac12552322d18947aac288b96246df03890da6555897878d5722" ||
        sha256_bytes(images.backup) !=
                "4daba0780e2edc9d8d96e98a8a6fd1c4ea96ace55c77ada8111c3fce7cb2e507") {
        throw std::runtime_error("Free A11 coherent4KiB GPT golden mismatch");
    }

    auto number = [](std::string_view data, std::size_t at) {
        std::uint64_t n = 0;

        for (unsigned i = 0; i < 8; ++i) {
            n |= std::uint64_t(static_cast<unsigned char>(data.at(at + i))) << (8 * i);
        }

        return n;
    };

    if (number(images.cache, 4096 + 32) != 64 || number(images.cache, 4096 + 40) != 36157 ||
        number(images.cache, 24) != 1 || number(images.cache, 32) != 65535 ||
        number(images.backup, 4096 + 24) != 65535 || number(images.backup, 4096 + 72) != 65534) {
        throw std::runtime_error("Free A11 GPT geometry");
    }

    if (0xaffc0000ULL + 64 * 4096 != 0xb0000000ULL ||
        0xaffc0000ULL + 36158 * 4096 != 0xb8cfe000ULL ||
        0xaffc0000ULL + 65534 * 4096 != 0xbffbe000ULL) {
        throw std::runtime_error("Free A11 physical reader bounds");
    }

    qinggan11_rejects([&] {
        qnx_qinggan11_gpt(image, std::string(64, '0'));
    });
    qinggan11_rejects([&] {
        qnx_qinggan11_gpt(std::string_view(image).substr(0, image.size() - 1), pin);
    });
    auto put = [](std::string &data, std::size_t at, std::uint32_t value) {
        for (unsigned i = 0; i < 4; ++i) {
            data.at(at + i) = static_cast<char>(value >> (8 * i));
        }
    };
    auto bad = image;
    put(bad, 8, 147839044);
    put(bad, 147839040, 0);
    std::uint32_t sum = 0;

    for (std::size_t at = 0; at < 147839044; at += 4) {
        std::uint32_t word = 0;

        for (unsigned i = 0; i < 4; ++i) {
            word |= std::uint32_t(static_cast<unsigned char>(bad[at + i])) << (8 * i);
        }

        sum += word;
    }

    put(bad, 147839040, 0 - sum);
    qinggan11_rejects([&] {
        qnx_qinggan11_loader_image(bad, sha256_bytes(bad));
    });

    for (unsigned which = 0; which < 3; ++which) {
        auto corrupt = images;
        auto &part = which == 0 ? corrupt.cache : which == 1 ? corrupt.primary : corrupt.backup;
        part[part.size() / 2] ^= 1;
        qinggan11_rejects([&] {
            validate_qnx_qinggan11_gpt(corrupt, image, pin);
        });
    }

    std::cout
            << "Free A11 exactImageFS checksum/extent, real4KiB GPT geometry/CRC/padding bounds and negative guards passed\n";
}
} // namespace

int main(int argc, char **argv) {
    try {
        bool reserve_rejected = false;
        try {
            prepare_qnx_system_disk({}, "", {}, (20ULL << 30) + 1);
        } catch (const std::exception &e) {
            reserve_rejected = std::string(e.what()).starts_with("QNX_GPT_RESERVE_BOUND");
        }

        if (!reserve_rejected) {
            throw std::runtime_error("GPT reserve overflow bound accepted");
        }

        qinggan11_rejects([] {
            qnx_qinggan11_gpt({}, {});
        });

        if (argc == 3 && std::string_view(argv[1]) == "--qinggan11-611") {
            std::ifstream in(argv[2], std::ios::binary);

            if (!in) {
                throw std::runtime_error("6.11 IFS input");
            }

            const std::string image((std::istreambuf_iterator<char>(in)), {}),
                    pin = "fe181358fb9f827bc9df2d63e7ba0fad4f5fb0264b3bfa43f9fa6d0b47bc3ad7";
            const auto gpt = qnx_qinggan11_611_gpt(image, pin);
            validate_qnx_qinggan11_611_gpt(gpt, image, pin);

            if (qnx_qinggan11_611_loader_image(image, pin).size() != 147832832) {
                throw std::runtime_error("6.11 loader extent");
            }

            qinggan11_rejects([&] {
                qnx_qinggan11_gpt(image, pin);
            });
            qinggan11_rejects([&] {
                qnx_qinggan11_611_gpt(image, std::string(64, '0'));
            });
            auto corrupt = gpt;
            corrupt.cache[0] ^= 1;
            qinggan11_rejects([&] {
                validate_qnx_qinggan11_611_gpt(corrupt, image, pin);
            });
            std::cout << "6.11 IFS GPT and cross-version guards passed\n";

            return 0;
        }

        if (argc == 3 && std::string_view(argv[1]) == "--qinggan11") {
            qinggan11_gpt_fixture(argv[2]);

            return 0;
        }

        loader_segments_fixture();
        const auto passion = qnx_passion_gpt(200925932);
        validate_qnx_passion_gpt(passion, 200925932);

        if (sha256_bytes(passion.primary) !=
                    "2c4dbce8e4030778302b5fd7c1354cd54e06a5278e52c8dade9ccb1eec29c478" ||
            sha256_bytes(passion.cache) !=
                    "e0d41d6a7369d9fe63a02e0eab3ea0f7b2a1e316ce4fe44d35657bec11420a95" ||
            sha256_bytes(passion.backup) !=
                    "21faa6656b9cb26cf4d091742049d061d526397cc67fc43f661aee192d5cf035") {
            throw std::runtime_error("Passion GPT golden mismatch");
        }

        for (unsigned part = 0; part < 3; ++part) {
            auto corrupt = passion;
            auto &data = part == 0 ? corrupt.cache : part == 1 ? corrupt.primary : corrupt.backup;
            data[data.size() / 2] ^= 1;
            bool rejected = false;
            try {
                validate_qnx_passion_gpt(corrupt, 200925932);
            } catch (...) {
                rejected = true;
            }

            if (!rejected) {
                throw std::runtime_error("Passion GPT mutation accepted");
            }
        }

        const auto gpt = qnx_synthetic_gpt();
        const auto a11 = qnx_synthetic_gpt_for_ifs2(0x10000000);

        if (a11 == gpt) {
            throw std::runtime_error("A11 IFS GPT extent unchanged");
        }

        for (auto size : {0ULL, 513ULL, 0x10000200ULL}) {
            bool rejected = false;
            try {
                qnx_synthetic_gpt_for_ifs2(size);
            } catch (...) {
                rejected = true;
            }

            if (!rejected) {
                throw std::runtime_error("IFS GPT invalid extent accepted");
            }
        }

        if (sha256_bytes(gpt) !=
            "301ae16d2a8867c5c6e1e85b76a71a813b1f1997a6902810f47d201d15401cb8") {
            throw std::runtime_error("synthetic GPT differs");
        }

        if (argc == 3) {
            const auto file = prepare_qnx_system_disk(
                    std::filesystem::absolute(argv[1]),
                    "ed32a05382848e0fb63cb33cdf0de55ec3d088f37aad21637b24c12e22a7a6bf",
                    std::filesystem::absolute(argv[2]));
            const auto actual = sha256_file(file);

            if (actual != "abac03ff5d8db7cf8c72c7a88c4037aac3f7b1c47b0cde20f4a96cf425b6dd56") {
                throw std::runtime_error("RAM GPT differs: " + actual);
            }

            std::cout
                    << "OEM system partition wrapped from raw input; exact accepted RAM-disk SHA verified\n";

            return 0;
        }

        std::cout << "synthetic QNX GPT exact SHA verified\n";

        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';

        return 1;
    }
}
