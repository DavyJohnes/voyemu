#include "apk_digest.hpp"
#include "model.hpp"
#include "process.hpp"
#include <fstream>
#include <iostream>
using namespace voyemu;

static std::string hex(const std::string &s) {
    std::string r;

    for (std::size_t i = 0; i < s.size(); i += 2) {
        r += char(std::stoul(s.substr(i, 2), nullptr, 16));
    }

    return r;
}

static void check(bool value) {
    if (!value) {
        throw std::runtime_error("APK v2 digest regression");
    }
}

int main() {
    try {
        const auto dir = std::filesystem::canonical(std::filesystem::temp_directory_path()) /
                         ("apk-digest-" + random_token());
        ensure_private_directory(dir);
        struct Cleanup {
            std::filesystem::path path;

            ~Cleanup() {
                std::error_code e;
                std::filesystem::remove_all(path, e);
            }
        } cleanup{dir};
        // Independent Python hashlib/zlib vectors: one chunk, exactly 1 MiB section, boundary+1.
        struct Vector {
            unsigned payload;
            const char *prefix;
            const char *suffix;
            const char *digest;
        };
        const Vector vectors[] = {
                {9,
                 "504b03041400000000000000000066deb77709000000090000000b000300636c61737365732e646578000"
                 "000",
                 "504b010214001400000000000000000066deb77709000000090000000b000000000000000000000000000"
                 "0000000636c61737365732e646578504b0506000000000100010039000000350000000000",
                 "ee1590db469e0ece9d75bfe9aee87f93e3b2716cbef42097f4e3e8706163e0a6"},
                {1048532,
                 "504b030414000000000000000000cf7f2d4bd4ff0f00d4ff0f000b000300636c61737365732e646578000"
                 "000",
                 "504b0102140014000000000000000000cf7f2d4bd4ff0f00d4ff0f000b000000000000000000000000000"
                 "0000000636c61737365732e646578504b0506000000000100010039000000000010000000",
                 "0514093782755729c9b838257f55716d77b42085720efc857eac57b1485b0594"},
                {1048533,
                 "504b0304140000000000000000001d4c27e3d5ff0f00d5ff0f000b000300636c61737365732e646578000"
                 "000",
                 "504b01021400140000000000000000001d4c27e3d5ff0f00d5ff0f000b000000000000000000000000000"
                 "0000000636c61737365732e646578504b0506000000000100010039000000010010000000",
                 "cb18b2b99d0d976fa51e17ada3eef7c60d1157ca7f577f3b889fd88950d2c15b"},
        };

        for (const auto &vector : vectors) {
            const auto bytes = hex(vector.prefix) + std::string(vector.payload, 'a') +
                               hex(vector.suffix);
            const auto file = dir / random_token();
            std::ofstream out(file, std::ios::binary);
            out.write(bytes.data(), bytes.size());
            out.close();
            const auto pin = sha256_file(file);
            check(unsigned_apk_v2_digest(file, pin) == vector.digest);
            bool rejected = false;
            try {
                unsigned_apk_v2_digest(file, std::string(64, '0'));
            } catch (...) {
                rejected = true;
            }

            check(rejected);
        }

        std::cout << "APK v2 chunked digest vectors passed\n";

        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';

        return 1;
    }
}
