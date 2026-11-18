#include "qnx_imagefs.hpp"
#include "process.hpp"
#include <algorithm>
#include <fstream>
#include <iostream>
using namespace voyemu;

static void duplicate_fixture() {
    std::string image(216, 0);

    image.replace(0, 8, std::string("imagefs\x04", 8));
    auto put = [&](std::size_t at, std::uint32_t value) {
        for (unsigned i = 0; i < 4; ++i) {
            image[at + i] = static_cast<char>(value >> (i * 8));
        }
    };
    put(8, 216);
    put(12, 200);
    put(16, 92);

    for (unsigned i = 0; i < 2; ++i) {
        const auto at = 92 + i * 52;
        put(at, 52);
        put(at + 8, 0100000);
        put(at + 24, 200 + i * 5);
        put(at + 28, 5 + i);
        image.replace(at + 32, 19, "lib64/graphics.conf");
    }

    image.replace(200, 5, "first");
    image.replace(205, 6, "second");
    std::uint32_t sum = 0;

    for (unsigned at = 0; at < 212; at += 4) {
        std::uint32_t value = 0;

        for (unsigned i = 0; i < 4; ++i) {
            value |= std::uint32_t(static_cast<unsigned char>(image[at + i])) << (i * 8);
        }

        sum += value;
    }

    put(212, 0 - sum);
    bool rejected = false;
    try {
        read_qnx_imagefs_file(image, "lib64/graphics.conf");
    } catch (...) {
        rejected = true;
    }

    if (!rejected) {
        throw std::runtime_error("duplicate ordinary read accepted");
    }

    const auto fixed = rename_qnx_imagefs_duplicate_file(image, "lib64/graphics.conf",
                                                         "lib64/old.conf", sha256_bytes("first"));

    if (read_qnx_imagefs_file(fixed, "lib64/old.conf") != "first" ||
        read_qnx_imagefs_file(fixed, "lib64/graphics.conf") != "second") {
        throw std::runtime_error("duplicate identity rename changed payload");
    }

    for (unsigned mode = 0; mode < 3; ++mode) {
        rejected = false;
        try {
            if (mode == 0) {
                rename_qnx_imagefs_duplicate_file(image, "lib64/graphics.conf", "lib64/old.conf",
                                                  std::string(64, '0'));
            } else if (mode == 1) {
                rename_qnx_imagefs_duplicate_file(fixed, "lib64/graphics.conf", "lib64/new.conf",
                                                  sha256_bytes("second"));
            } else {
                auto corrupt = image;
                corrupt[201] ^= 1;
                rename_qnx_imagefs_duplicate_file(corrupt, "lib64/graphics.conf", "lib64/old.conf",
                                                  sha256_bytes("first"));
            }
        } catch (...) {
            rejected = true;
        }

        if (!rejected) {
            throw std::runtime_error("bad duplicate identity/count/checksum accepted");
        }
    }
}

static void grow_fixture() {
    auto need = [](bool yes) {
        if (!yes) {
            throw std::runtime_error("ImageFS zero-padding growth invariant");
        }
    };
    auto put = [](std::string &b, std::size_t at, std::uint32_t value) {
        for (unsigned i = 0; i < 4; ++i) {
            b[at + i] = char(value >> (i * 8));
        }
    };
    auto checksum = [&](std::string &b) {
        put(b, b.size() - 4, 0);
        std::uint32_t sum = 0;

        for (std::size_t a = 0; a < b.size(); a += 4) {
            std::uint32_t v = 0;

            for (unsigned i = 0; i < 4; ++i) {
                v |= std::uint32_t(static_cast<unsigned char>(b[a + i])) << (8 * i);
            }

            sum += v;
        }

        put(b, b.size() - 4, 0 - sum);
    };
    std::string image(512, 0);

    image.replace(0, 8, std::string("imagefs\x04", 8));
    put(image, 8, 512);
    put(image, 12, 176);
    put(image, 16, 92);

    for (unsigned n = 0; n < 2; ++n) {
        const auto at = 92 + n * 40;
        put(image, at, 40);
        put(image, at + 8, 0100000);
        put(image, at + 24, n ? 320 : 256);
        put(image, at + 28, 4);
        image.replace(at + 32, 1, n ? "b" : "a");
    }

    image.replace(256, 4, "old!");
    image.replace(320, 4, "keep");
    checksum(image);
    const auto out = replace_qnx_imagefs_file_in_zero_padding(image, "a", sha256_bytes("old!"),
                                                              "longer-value", 16);
    need(out.size() == image.size() && read_qnx_imagefs_file(out, "a") == "longer-value" &&
         read_qnx_imagefs_file(out, "b") == "keep");

    for (std::size_t i = 0; i < image.size(); ++i) {
        if (!(i >= 256 && i < 268) && !(i >= 120 && i < 124) && i < 508) {
            need(out[i] == image[i]);
        }
    }

    auto refused_inner = [&](auto &&put_local) {
        for (unsigned mode = 0; mode < 9; ++mode) {
            bool refused = false;
            auto bad = image;
            try {
                if (mode == 0) {
                    replace_qnx_imagefs_file_in_zero_padding(image, "a", std::string(64, '0'),
                                                             "longer-value", 16);
                } else if (mode == 1) {
                    bad[260] = 1;
                    checksum(bad);
                    replace_qnx_imagefs_file_in_zero_padding(bad, "a", sha256_bytes("old!"),
                                                             "longer-value", 16);
                } else if (mode == 2) {
                    put_local(bad, 156, 260);
                    checksum(bad);
                    replace_qnx_imagefs_file_in_zero_padding(bad, "a", sha256_bytes("old!"),
                                                             "longer-value", 16);
                } else if (mode == 3) {
                    replace_qnx_imagefs_file_in_zero_padding(image, "a", sha256_bytes("old!"),
                                                             std::string(260, 'a'), 512);
                } else if (mode == 4) {
                    replace_qnx_imagefs_file_in_zero_padding(image, "a", sha256_bytes("old!"),
                                                             "longer-value", 1);
                } else if (mode == 5) {
                    replace_qnx_imagefs_file_in_zero_padding(image, "a", sha256_bytes("old!"),
                                                             "old!", 16);
                } else if (mode == 6) {
                    replace_qnx_imagefs_file_in_zero_padding(out, "a", sha256_bytes("old!"),
                                                             "longer-value!", 16);
                } else if (mode == 7) {
                    bad[256] ^= 1;
                    replace_qnx_imagefs_file_in_zero_padding(bad, "a", sha256_bytes("old!"),
                                                             "longer-value", 16);
                } else {
                    replace_qnx_imagefs_file_in_zero_padding(image, "../a", sha256_bytes("old!"),
                                                             "longer-value", 16);
                }
            } catch (...) {
                refused = true;
            }

            need(refused);
        }
    };
    refused_inner(put);
    // Overflow of the slot the OEM image provides is a coded capacity error, not a bare code.
    auto capacity = [](auto &&call) {
        try {
            call();
        } catch (const std::runtime_error &error) {
            return std::string_view(error.what())
                    .starts_with("QNX_IMAGEFS_SLOT_CAPACITY: a needs ");
        }

        return false;
    };
    auto dirty = image;
    dirty[260] = 1;
    checksum(dirty);
    need(capacity([&] {
        replace_qnx_imagefs_file(image, "a", sha256_bytes("old!"), "old!!", 1, false);
    }));
    need(capacity([&] {
        replace_qnx_imagefs_file_in_zero_padding(image, "a", sha256_bytes("old!"), "longer-value",
                                                 4);
    }));
    need(capacity([&] {
        replace_qnx_imagefs_file_in_zero_padding(image, "a", sha256_bytes("old!"),
                                                 std::string(65, 'a'), 512);
    }));
    need(capacity([&] {
        replace_qnx_imagefs_file_in_zero_padding(dirty, "a", sha256_bytes("old!"), "longer-value",
                                                 16);
    }));
}

static void pad_fixture() {
    auto need = [](bool yes) {
        if (!yes) {
            throw std::runtime_error("ImageFS first-member pad invariant");
        }
    };
    auto put = [](std::string &b, std::size_t at, std::uint32_t value) {
        for (unsigned i = 0; i < 4; ++i) {
            b[at + i] = char(value >> (i * 8));
        }
    };
    auto checksum = [&](std::string &b, std::size_t extent) {
        put(b, extent - 4, 0);
        std::uint32_t sum = 0;

        for (std::size_t a = 0; a < extent; a += 4) {
            std::uint32_t v = 0;

            for (unsigned i = 0; i < 4; ++i) {
                v |= std::uint32_t(static_cast<unsigned char>(b[a + i])) << (8 * i);
            }

            sum += v;
        }

        put(b, extent - 4, 0 - sum);
    };
    auto get = [](std::string_view b, std::size_t at) {
        std::uint32_t v = 0;

        for (unsigned i = 0; i < 4; ++i) {
            v |= std::uint32_t(static_cast<unsigned char>(b[at + i])) << (8 * i);
        }

        return v;
    };
    std::string inner(128, 0);

    inner.replace(0, 8, std::string("imagefs\x04", 8));
    put(inner, 8, 128);
    put(inner, 12, 96);
    put(inner, 16, 64);
    put(inner, 64, 40);
    put(inner, 72, 0100000);
    put(inner, 88, 96);
    put(inner, 92, 8);
    inner.replace(96, 8, "nested!!");
    checksum(inner, 128);
    std::string image(1280, 0);
    image.replace(0, 8, std::string("imagefs\x04", 8));
    put(image, 8, 1164);
    put(image, 12, 176);
    put(image, 16, 92);
    put(image, 92, 40);
    put(image, 100, 0100000);
    put(image, 116, 1024);
    put(image, 120, 128);
    image.replace(92 + 32, 2, "a");
    put(image, 132, 40);
    put(image, 140, 0100000);
    put(image, 156, 1152);
    put(image, 160, 8);
    image.replace(132 + 32, 2, "b");
    image.replace(1024, 128, inner);
    image.replace(1152, 8, "keep!!!!");
    checksum(image, 1164);
    const auto out = pad_qnx_imagefs_first_member(image, 16);
    need(out.size() == image.size() + 16 && get(out, 8) == 1180 && get(out, 120) == 144 &&
         get(out, 156) == 1168);
    auto nested_check = inner;
    put(nested_check, 8, 144);
    need(read_qnx_imagefs_file(out, "a") == nested_check + std::string(16, '\0') &&
         read_qnx_imagefs_file(out, "b") == "keep!!!!");

    for (std::size_t i = 0; i < 1152; ++i) {
        if (!(i >= 8 && i < 12) && !(i >= 120 && i < 124) && !(i >= 156 && i < 160) &&
            !(i >= 1032 && i < 1036)) {
            need(out[i] == image[i]);
        }
    }

    need(std::string_view(out).substr(1152, 16) == std::string(16, '\0'));

    for (std::size_t i = 1168; i < out.size(); ++i) {
        if (!(i >= 1176 && i < 1180)) {
            need(out[i] == image[i - 16]);
        }
    }

    for (unsigned mode = 0; mode < 7; ++mode) {
        bool refused = false;
        auto bad = image;
        try {
            if (mode == 0) {
                pad_qnx_imagefs_first_member(image, 0);
            } else if (mode == 1) {
                pad_qnx_imagefs_first_member(image, 6);
            } else if (mode == 2) {
                pad_qnx_imagefs_first_member(image, 8192);
            } else if (mode == 3) {
                bad[1160] ^= 1;
                pad_qnx_imagefs_first_member(bad, 16);
            } else if (mode == 4) {
                put(bad, 8, 1160);
                checksum(bad, 1160);
                pad_qnx_imagefs_first_member(bad, 16);
            } else if (mode == 5) {
                put(bad, 128, 1144);
                put(bad, 8, 1156);
                checksum(bad, 1156);
                pad_qnx_imagefs_first_member(bad, 4);
            } else if (mode == 6) {
                put(bad, 1032, 120);
                checksum(bad, 1164);
                pad_qnx_imagefs_first_member(bad, 16);
            }
        } catch (...) {
            refused = true;
        }

        need(refused);
    }

    for (unsigned mode = 0; mode < 2; ++mode) {
        bool refused = false;
        auto bad = image;
        try {
            if (mode == 0) {
                put(bad, 92, 200);
                checksum(bad, 1164);
                pad_qnx_imagefs_first_member(bad, 16);
            } else {
                auto corrupt = image;
                corrupt[8] ^= 1;
                pad_qnx_imagefs_first_member(corrupt, 16);
            }
        } catch (...) {
            refused = true;
        }

        need(refused);
    }
}

int main(int argc, char **argv) {
    try {
        duplicate_fixture();
        grow_fixture();
        pad_fixture();

        if (argc == 4 && std::string_view(argv[1]) == "--qinggan11-growth") {
            auto read = [](const char *path) {
                std::ifstream in(path, std::ios::binary);

                if (!in) {
                    throw std::runtime_error("growth fixture open");
                }

                return std::string((std::istreambuf_iterator<char>(in)), {});
            };
            const auto host = read(argv[2]), script = read(argv[3]);

            if (sha256_bytes(host) !=
                "67b3ee50b6a4d5ff817986a769e3cddc6d628df8ede54830319783421aca85ad") {
                throw std::runtime_error("Free host source SHA");
            }

            const auto original = read_qnx_imagefs_file(host, "proc/boot/.script"),
                       out = replace_qnx_imagefs_file_in_zero_padding(
                               host, "proc/boot/.script", sha256_bytes(original), script, 300);

            if (out.size() != host.size() ||
                read_qnx_imagefs_file(out, "proc/boot/.script") != script) {
                throw std::runtime_error("actual boot slot growth");
            }

            for (std::size_t i = 0; i < host.size(); ++i) {
                if (!(i >= 912632 && i < 912632 + script.size()) && !(i >= 1260 && i < 1264) &&
                    !(i >= host.size() - 4)) {
                    if (host[i] != out[i]) {
                        throw std::runtime_error("actual unrelated ImageFS byte changed");
                    }
                }
            }

            std::cout
                    << "Actual Free boot script grows only within 300 zero bytes; all other ImageFS bytes, extent, data offsets and valid checksum preserved\n";

            return 0;
        }

        if (argc == 3 && std::string(argv[1]) == "--recovery") {
            std::ifstream in(argv[2], std::ios::binary);

            if (!in) {
                throw std::runtime_error("recovery input open");
            }

            const std::string image((std::istreambuf_iterator<char>(in)), {});

            if (sha256_bytes(image) !=
                "d7dd2e04b36d760dd8e95a939e194530e66b24b8ffe77a5f7290684145e76930") {
                throw std::runtime_error("recovery input SHA");
            }

            const auto recovery = read_qnx_imagefs_file(image, "ifs_recovery.img"),
                       driver = read_qnx_imagefs_file(recovery, "bin/devb-ram");

            if (sha256_bytes(driver) !=
                "53c732c493495256d16ebe39ab0e0a0f2bbdce3b600b45b5e7d770bd02d4f780") {
                throw std::runtime_error("recovery RAM driver SHA");
            }

            std::cout
                    << "RAM driver extracted from same-firmware OEM recovery; exact SHA verified\n";

            return 0;
        }

        if (argc == 2) {
            std::ifstream in(argv[1], std::ios::binary);

            if (!in) {
                throw std::runtime_error("OEM input open");
            }

            const std::string image((std::istreambuf_iterator<char>(in)), {});

            if (sha256_bytes(image) !=
                "403269cadb0b279a8698eddf909948408caf0e2d5bd2423fd6a11e58dcde0ee0") {
                throw std::runtime_error("OEM input SHA");
            }

            const auto display = read_qnx_imagefs_file(image, "ifs_display.img"),
                       screen = read_qnx_imagefs_file(display, "bin/screen");

            if (sha256_bytes(screen) !=
                "afd9ec92d33c4d51d3559b72f911a3abfab4e5d8f850b5809edb76956bd432d0") {
                throw std::runtime_error("OEM Screen SHA");
            }

            const auto renamed = rename_qnx_imagefs_file(display, "proc/boot/logo.jpg", "logo.jpg");

            if (read_qnx_imagefs_file(renamed, "logo.jpg") !=
                read_qnx_imagefs_file(display, "proc/boot/logo.jpg")) {
                throw std::runtime_error("rename changed payload");
            }

            const auto same = replace_qnx_imagefs_file(display, "bin/screen", sha256_bytes(screen),
                                                       screen);

            if (same != display) {
                throw std::runtime_error("identity replacement changed image");
            }

            const auto changed = replace_qnx_imagefs_file(
                    display, "bin/Snapdragon.rgb",
                    sha256_bytes(read_qnx_imagefs_file(display, "bin/Snapdragon.rgb")),
                    "owned guest bridge", 4096);

            if (read_qnx_imagefs_file(changed, "bin/Snapdragon.rgb") != "owned guest bridge") {
                throw std::runtime_error("aligned replacement failed");
            }

            bool failed = false;
            try {
                replace_qnx_imagefs_file(display, "bin/screen", sha256_bytes(screen),
                                         std::string(screen.size() + 1, 0));
            } catch (...) {
                failed = true;
            }

            if (!failed) {
                throw std::runtime_error("oversized replacement accepted");
            }

            failed = false;
            auto corrupt = display;
            corrupt[100] ^= 1;
            try {
                read_qnx_imagefs_file(corrupt, "bin/screen");
            } catch (...) {
                failed = true;
            }

            if (!failed) {
                throw std::runtime_error("corrupt ImageFS accepted");
            }

            std::cout
                    << "nested OEM Screen extracted from raw IFS2 with exact SHA; corrupt checksum rejected\n";

            return 0;
        }

        bool failed = false;
        try {
            read_qnx_imagefs_file("", "bin/screen");
        } catch (...) {
            failed = true;
        }

        if (!failed) {
            throw std::runtime_error("empty ImageFS accepted");
        }

        std::cout << "ImageFS input guards passed\n";

        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';

        return 1;
    }
}
