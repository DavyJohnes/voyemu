#include "digest.hpp"
#include <cstdint>
#include <cstdio>
#include <exception>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

namespace {
int failures = 0;

void expect(const std::string &actual, const char *expected, const char *label) {
    if (actual != expected) {
        std::fprintf(stderr, "%s: %s != %s\n", label, actual.c_str(), expected);
        ++failures;
    }
}

std::string sha256(const std::string &text, std::size_t chunk,
                   voyemu::os::Sha256Engine engine = voyemu::os::sha256_engines().back()) {
    voyemu::os::Sha256 hash(engine);

    for (std::size_t offset = 0; offset < text.size(); offset += chunk) {
        hash.update(text.data() + offset, std::min(chunk, text.size() - offset));
    }

    return voyemu::os::hex_digest(hash.finish());
}

std::string md5(const std::string &text, std::size_t chunk) {
    voyemu::os::Md5 hash;

    for (std::size_t offset = 0; offset < text.size(); offset += chunk) {
        hash.update(text.data() + offset, std::min(chunk, text.size() - offset));
    }

    return voyemu::os::hex_digest(hash.finish());
}

// Every engine this CPU runs hashes the same random bytes, split at random points, to one digest.
void compare_engines(const std::vector<voyemu::os::Sha256Engine> &engines) {
    // A fixed xorshift sequence keeps failures reproducible.
    std::uint64_t state = 0x9e3779b97f4a7c15ULL;
    const auto random = [&state] {
        state ^= state << 13;
        state ^= state >> 7;
        state ^= state << 17;

        return state;
    };
    std::string bytes(std::size_t(3) << 20, '\0');

    for (auto &byte : bytes) {
        byte = char(random());
    }

    for (const std::size_t size : {std::size_t(0), std::size_t(1), std::size_t(63), std::size_t(64),
                                   std::size_t(65), std::size_t(4097), bytes.size()}) {
        const auto input = bytes.substr(0, size);
        const auto expected = sha256(input, input.size() + 1, voyemu::os::Sha256Engine::portable);

        for (const auto engine : engines) {
            voyemu::os::Sha256 hash(engine);

            for (std::size_t offset = 0; offset < input.size();) {
                const auto chunk = std::min<std::size_t>(random() % 200000, input.size() - offset);
                hash.update(input.data() + offset, chunk);
                offset += chunk;
            }

            expect(voyemu::os::hex_digest(hash.finish()), expected.c_str(),
                   "sha256 engines agree on random input");
        }
    }
}
} // namespace

int main() try {
    const auto engines = voyemu::os::sha256_engines();

    if (engines.front() != voyemu::os::Sha256Engine::portable) {
        std::fprintf(stderr, "portable sha256 engine missing\n");
        ++failures;
    }

    std::fprintf(stderr, "sha256 engines: %zu\n", engines.size());
    compare_engines(engines);
    const std::string million(1000000, 'a');
    const std::string two_blocks = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    const std::string four_blocks = "abcdefghbcdefghicdefghijdefghijkefghijklfghijklmghijklmn"
                                    "hijklmnoijklmnopjklmnopqklmnopqrlmnopqrsmnopqrstnopqrstu";

    for (const auto engine : engines) {
        for (std::size_t chunk :
             {std::size_t(1), std::size_t(7), std::size_t(64), std::size_t(65536)}) {
            for (const auto &[bytes, expected] :
                 {std::pair{55, "9f4390f8d30c2dd92ec9f095b65e2b9ae9b0a925a5258e241c9f1e910f734318"},
                  std::pair{56, "b35439a4ac6f0948b6d6f9e3c6af0f5f590ce20f1bde7090ef7970686ec6738a"},
                  std::pair{63, "7d3e74a05d7db15bce4ad9ec0658ea98e3f06eeecf16b4c6fff2da457ddc2f34"},
                  std::pair{64, "ffe054fe7ae0cb6dc65c3af9b61d5209f439851db43d0ba5997337df154668eb"},
                  std::pair{65,
                            "635361c48bb9eab14198e76ea8ab7f1a41685d6ad62aa9146d301d4f17eb0ae0"}}) {
                expect(sha256(std::string(bytes, 'a'), chunk, engine), expected,
                       "sha256 padding boundary");
            }

            expect(sha256("", chunk, engine),
                   "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
                   "sha256 empty");
            expect(sha256("abc", chunk, engine),
                   "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
                   "sha256 abc");
            expect(sha256(two_blocks, chunk, engine),
                   "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1",
                   "sha256 448 bits");
            expect(sha256(four_blocks, chunk, engine),
                   "cf5b16a778af8380036ce59e7b0492370b249b11e8f07a51afac45037afee9d1",
                   "sha256 896 bits");
            expect(md5("", chunk), "d41d8cd98f00b204e9800998ecf8427e", "md5 empty");
            expect(md5("abc", chunk), "900150983cd24fb0d6963f7d28e17f72", "md5 abc");
            expect(md5("12345678901234567890123456789012345678901234567890123456789012345678901234567890",
                       chunk),
                   "57edf4a22be3c955ac49da2e2107b67a", "md5 80 digits");
        }

        expect(sha256(million, 4096, engine),
               "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0",
               "sha256 million");
        expect(md5(million, 4096), "7707d6ae4e027c70eea2a935c2296f21", "md5 million");
    }

    voyemu::os::Sha256 reused;
    reused.update("abc", 3);
    reused.finish();
    expect(voyemu::os::hex_digest(reused.finish()),
           "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855", "sha256 reset");

    return failures ? 1 : 0;
} catch (const std::exception &error) {
    std::cerr << error.what() << '\n';

    return 1;
}
