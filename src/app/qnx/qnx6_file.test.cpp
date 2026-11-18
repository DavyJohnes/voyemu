#include "qnx6_file.hpp"
#include "process.hpp"
#include <fstream>
#include <functional>
#include <iostream>
using namespace voyemu;
namespace {
void need(bool value, const char *reason) {
    if (!value) {
        throw std::runtime_error(reason);
    }
}

void put(std::string &out, std::size_t at, std::uint64_t value, unsigned width) {
    need(at + width <= out.size(), "fixture write bounds");

    for (unsigned i = 0; i < width; ++i) {
        out[at + i] = static_cast<char>(value >> (8 * i));
    }
}

std::uint32_t put32(const std::string &in, std::size_t at) {
    need(at + 4 <= in.size(), "fixture read bounds");

    return std::uint32_t(static_cast<unsigned char>(in[at])) |
           std::uint32_t(static_cast<unsigned char>(in[at + 1])) << 8 |
           std::uint32_t(static_cast<unsigned char>(in[at + 2])) << 16 |
           std::uint32_t(static_cast<unsigned char>(in[at + 3])) << 24;
}

std::uint32_t super_crc(std::string_view data) {
    std::uint32_t value = 0;

    for (unsigned char byte : data) {
        value ^= std::uint32_t(byte) << 24;

        for (unsigned i = 0; i < 8; ++i) {
            value = (value << 1) ^ ((value & 0x80000000) ? 0x04c11db7 : 0);
        }
    }

    return value;
}

std::uint32_t name_sum(std::string_view data) {
    std::uint32_t value = 0;

    for (unsigned char byte : data) {
        value = ((value >> 1) | (value << 31)) + byte;
    }

    return value;
}

constexpr std::size_t blocksize = 512, blocks = 64, data_start = 0x3000,
                      backup = data_start + blocks * blocksize;
constexpr auto long_directory = "a-directory-name-longer-than-27-characters";
constexpr auto long_file = "a-library-name-longer-than-27-characters.so.5.15.15";
constexpr std::string_view short_file = "sssssssssssssssssssssssssss";

void root_node(std::string &data, std::size_t at, std::uint64_t size, unsigned block,
               unsigned levels = 0) {
    put(data, at, size, 8);

    for (unsigned i = 0; i < 16; ++i) {
        put(data, at + 8 + static_cast<std::size_t>(i) * 4, i ? UINT32_MAX : block, 4);
    }

    put(data, at + 72, levels, 1);
}

void fix_super(std::string &data, std::size_t at) {
    put(data, at + 4, super_crc(std::string_view(data).substr(at + 8, 504)), 4);
}

void inode(std::string &data, unsigned id, unsigned mode, unsigned size, unsigned block) {
    const auto at = data_start + static_cast<std::size_t>(id - 1) * 128;

    put(data, at, size, 8);
    put(data, at + 32, mode, 2);

    for (unsigned i = 0; i < 16; ++i) {
        put(data, at + 36 + static_cast<std::size_t>(i) * 4, i ? UINT32_MAX : block, 4);
    }
}

void short_entry(std::string &data, std::size_t at, unsigned id, std::string_view name) {
    need(name.size() <= 27, "short fixture name");
    put(data, at, id, 4);
    put(data, at + 4, name.size(), 1);
    data.replace(at + 5, name.size(), name);
}

void long_entry(std::string &data, std::size_t at, unsigned id, unsigned location,
                std::string_view name) {
    put(data, at, id, 4);
    put(data, at + 4, 255, 1);
    put(data, at + 8, location, 4);
    put(data, at + 12, name_sum(name), 4);
    const auto record = data_start + (5 + location) * blocksize;
    put(data, record, name.size(), 2);
    data.replace(record + 2, name.size(), name);
}

std::string fixture() {
    std::string data(backup + 512, 0);

    put(data, 0x2000, 0x68191122, 4);
    put(data, 0x2000 + 8, 2, 8);
    put(data, 0x2000 + 48, blocksize, 4);
    put(data, 0x2000 + 52, 4, 4);
    put(data, 0x2000 + 60, blocks, 4);
    root_node(data, 0x2000 + 72, 512, 0);
    root_node(data, 0x2000 + 232, 3 * blocksize, 4, 1);
    put(data, data_start + 4 * blocksize, 5, 4);
    put(data, data_start + 4 * blocksize + 4, 6, 4);
    put(data, data_start + 4 * blocksize + 8, 7, 4);
    inode(data, 1, 0040755, 128, 1);
    inode(data, 2, 0100644, 13, 2);
    inode(data, 3, 0100644, 12, 3);
    inode(data, 4, 0040755, 32, 8);
    data.replace(data_start + 2 * blocksize, 13, "owned-payload");
    data.replace(data_start + 3 * blocksize, 12, "short-reader");
    const auto dir = data_start + blocksize;
    long_entry(data, dir, 4, 0, long_directory);
    short_entry(data, dir + 32, 3, short_file);
    long_entry(data, dir + 64, 2, 1, long_file);
    long_entry(data, dir + 96, 2, 2, std::string(510, 'z'));
    long_entry(data, data_start + 8 * blocksize, 2, 1, long_file);
    fix_super(data, 0x2000);
    data.replace(backup, 512, data.substr(0x2000, 512));
    put(data, backup + 8, 1, 8);
    root_node(data, backup + 232, 0, UINT32_MAX);
    fix_super(data, backup);

    return data;
}

void fixtures() {
    const auto directory = std::filesystem::current_path() / ".local" / "build" / "tmp" /
                           ("qnx6-longnames-" + random_token());

    need(std::filesystem::create_directories(directory), "fixture directory");
    struct Cleanup {
        std::filesystem::path directory;

        ~Cleanup() {
            std::error_code ec;
            std::filesystem::remove_all(directory, ec);
        }
    } cleanup{directory};
    const auto image = std::filesystem::absolute(directory / "fixture.img");
    auto read = [&](const std::string &data, std::string_view path) {
        std::ofstream out(image, std::ios::binary | std::ios::trunc);
        out.write(data.data(), data.size());
        out.close();
        need(bool(out), "fixture image write");

        return read_qnx6_file(image, sha256_bytes(data), path);
    };
    const auto data = fixture();
    need(std::string_view(short_file).size() == 27, "short fixture boundary");
    need(read(data, short_file) == "short-reader", "short name regression");
    need(read(data, long_file) == "owned-payload", "long filename lookup");
    validate_qnx6_image(image);
    replace_qnx6_file(image, sha256_bytes(data), long_file, sha256_bytes("owned-payload"),
                      "fixed-payload");
    need(read_qnx6_file(image, sha256_file(image), long_file) == "fixed-payload",
         "equal size QNX6 replacement");
    {
        std::ifstream in(image, std::ios::binary);
        const std::string actual((std::istreambuf_iterator<char>(in)), {});
        auto expected = data;
        expected.replace(data_start + 2 * blocksize, 13, "fixed-payload");
        need(actual == expected, "replacement changed metadata or another file");
    }

    for (const auto bad : {0, 1, 2}) {
        const auto pin = sha256_file(image);
        bool failed = false;
        try {
            replace_qnx6_file(image, bad == 0 ? std::string(64, '0') : pin, long_file,
                              bad == 1 ? std::string(64, '0') : sha256_bytes("fixed-payload"),
                              bad == 2 ? "short" : "owned-payload");
        } catch (...) {
            failed = true;
        }

        need(failed && sha256_file(image) == pin, "invalid replacement modified image");
    }

    {
        const auto wrapped = std::string(32768, 0) + data;
        std::ofstream out(image, std::ios::binary | std::ios::trunc);
        out.write(wrapped.data(), wrapped.size());
        out.close();
        replace_qnx6_file(image, sha256_bytes(wrapped), long_file, sha256_bytes("owned-payload"),
                          "fixed-payload", 32768);
        std::ifstream in(image, std::ios::binary);
        const std::string actual((std::istreambuf_iterator<char>(in)), {});
        auto expected = wrapped;
        expected.replace(32768 + data_start + 2 * blocksize, 13, "fixed-payload");
        need(actual == expected, "wrapped partition replacement changed another extent");
    }

    need(read(data, std::string(long_directory) + "/" + long_file) == "owned-payload",
         "long directory lookup");
    need(read(data, std::string(510, 'z')) == "owned-payload", "maximum 510-byte name");
    auto rejected = [&](std::string changed, std::string_view path, std::string_view error) {
        bool caught = false;
        try {
            (void)read(changed, path);
        } catch (const std::exception &failure) {
            caught = std::string_view(failure.what()).find(error) != std::string_view::npos;
        }

        need(caught, "malformed QNX6 fixture not rejected at expected guard");
    };
    auto changed = data;
    changed[data_start + blocksize + 64 + 12] ^= 1;
    rejected(changed, long_file, "QNX6_LONGNAME_CHECKSUM");
    changed = data;
    put(changed, data_start + blocksize + 64 + 8, 3, 4);
    rejected(changed, long_file, "QNX6_FILE_BOUNDS");
    changed = data;
    put(changed, data_start + 6 * blocksize, 511, 2);
    rejected(changed, long_file, "QNX6_LONGNAME_LENGTH");
    changed = data;
    put(changed, data_start + 6 * blocksize, 27, 2);
    rejected(changed, long_file, "QNX6_LONGNAME_LENGTH");
    changed = data;
    put(changed, data_start + 4 * blocksize + 4, blocks, 4);
    rejected(changed, long_file, "QNX6_BLOCK_POINTER");
    changed = data;
    put(changed, data_start + blocksize + 64 + 4, 28, 1);
    rejected(changed, long_file, "QNX6_DIRECTORY_NAME");
    changed = data;
    changed.replace(data_start + blocksize + 32, 32, data.substr(data_start + blocksize + 64, 32));
    rejected(changed, long_file, "QNX6_AMBIGUOUS_ENTRY");
    changed = data;
    changed[data_start + 6 * blocksize + 3] = '/';
    put(changed, data_start + blocksize + 64 + 12,
        name_sum(std::string_view(changed).substr(data_start + 6 * blocksize + 2,
                                                  std::string_view(long_file).size())),
        4);
    rejected(changed, long_file, "QNX6_DIRECTORY_NAME");
    changed = data;
    put(changed, 0x2000 + 8, 0, 8);
    fix_super(changed, 0x2000);
    rejected(changed, long_file, "QNX6_FILE_BOUNDS");
    rejected(data, std::string(511, 'z'), "QNX6_PATH_COMPONENT");
    rejected(data, "../outside", "QNX6_PATH_COMPONENT");
}

void system_initialization_fixtures() {
    const auto directory = std::filesystem::absolute(std::filesystem::current_path() /
                                                     ".local/build/tmp" /
                                                     ("qnx6-system-" + random_token()));

    need(std::filesystem::create_directories(directory), "system fixture directory");
    struct Cleanup {
        std::filesystem::path path;

        ~Cleanup() {
            std::error_code e;
            std::filesystem::remove_all(path, e);
        }
    } cleanup{directory};
    const auto source = directory / "seed.img", stopped = directory / "stopped.img",
               again = directory / "again.img";
    auto volume = fixture();
    inode(volume, 1, 0040755, 32, 1);
    inode(volume, 2, 0040755, 32, 2);
    inode(volume, 3, 0040755, 32, 3);
    inode(volume, 4, 0100600, 13, 8);
    short_entry(volume, data_start + blocksize, 2, "etc");
    short_entry(volume, data_start + 2 * blocksize, 3, "clsboot");
    short_entry(volume, data_start + 3 * blocksize, 4, "boot_cfg.xml");
    volume.replace(data_start + 8 * blocksize, 13, "<boot></boot>");
    auto wrap = [&](const std::string &data) {
        std::string image(32768, 0);
        image.replace(512, 8, "EFI PART");
        put(image, 524, 92, 4);
        put(image, 584, 2, 8);
        put(image, 592, 33, 4);
        put(image, 596, 128, 4);
        put(image, 1056, 64, 8);
        put(image, 1064, 64 + data.size() / 512 - 1, 8);
        image += data;
        image.resize(image.size() + 34 * 512 + 4096, 0);

        return image;
    };
    auto write = [&](const std::filesystem::path &path, const std::string &bytes) {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out.write(bytes.data(), bytes.size());
        out.close();
        need(bool(out), "system fixture write");
    };
    const auto final = wrap(volume);
    auto initial = volume;
    inode(initial, 3, 0040755, 0, 3);
    const auto seed = wrap(initial);
    write(source, seed);
    write(stopped, final);
    need(sha256_file(source) != sha256_file(stopped), "initialization changed hash");
    need(validate_qnx6_system_initialization(source, sha256_file(source), stopped,
                                             sha256_file(stopped)) == "<boot></boot>",
         "wrapped system initialization");
    write(again, final);
    need(validate_qnx6_system_initialization(stopped, sha256_file(stopped), again,
                                             sha256_file(again)) == "<boot></boot>",
         "existing regular config idempotent");
    bool raw_rejected = false;
    try {
        read_qnx6_file(stopped, sha256_file(stopped), "etc/clsboot/boot_cfg.xml");
    } catch (...) {
        raw_rejected = true;
    }

    need(raw_rejected, "raw API must not guess GPT offset");
    auto rejects = [&](std::string bytes) {
        write(stopped, bytes);
        bool caught = false;
        try {
            validate_qnx6_system_initialization(source, sha256_file(source), stopped,
                                                sha256_file(stopped));
        } catch (...) {
            caught = true;
        }

        need(caught, "malformed initialized system accepted");
    };

    for (const auto offset : {512ULL, 1024ULL, 32768ULL + volume.size(),
                              static_cast<unsigned long long>(final.size() - 1)}) {
        auto bad = final;
        bad[offset] ^= 1;
        rejects(bad);
    }

    auto bad = final;
    bad.pop_back();
    rejects(bad);
    bad = final;
    bad[32768 + 0x2000 + 4] ^= 1;
    rejects(bad);
    bad = wrap(initial);
    rejects(bad);

    for (const auto mode : {0120600U, 0040600U, 0100644U}) {
        auto changed = volume;
        put(changed, data_start + 3 * 128 + 32, mode, 2);
        rejects(wrap(changed));
    }

    auto changed = volume;
    put(changed, data_start + 3 * 128 + 36, blocks, 4);
    rejects(wrap(changed));
    changed = volume;
    put(changed, data_start + 3 * 128 + 36, blocks + 8, 4);
    rejects(wrap(changed));
    write(stopped, final);
    bool pin_rejected = false;
    try {
        validate_qnx6_system_initialization(source, std::string(64, '0'), stopped,
                                            sha256_file(stopped));
    } catch (...) {
        pin_rejected = true;
    }

    need(pin_rejected, "source pin changed");
    bool alias_rejected = false;
    try {
        validate_qnx6_system_initialization(source, sha256_file(source), source,
                                            sha256_file(source));
    } catch (...) {
        alias_rejected = true;
    }

    need(alias_rejected, "system seed alias accepted");
}
} // namespace

int main(int argc, char **argv) {
    try {
        if (argc == 3 && std::string_view(argv[1]) == "--validate") {
            validate_qnx6_image(std::filesystem::absolute(argv[2]));
            std::cout << "validated\n";

            return 0;
        }

        fixtures();
        system_initialization_fixtures();

        if (argc == 4 && std::string_view(argv[1]) == "--qinggan11-731-themes") {
            const auto image = std::filesystem::canonical(argv[2]),
                       out = std::filesystem::canonical(argv[3]);
            const std::string pin =
                    "4e611fab0db36483422a8ce9face07b5848085a21a974e35c4efa20adc812188";

            for (const auto &item : std::array<std::pair<std::string_view, std::string_view>, 2>{
                         {{"usr/bin/themmgr", "themmgr-731"},
                          {"lib64/libclusterscene_withoutfaceid.so.0.1", "cluster-scene-731"}}}) {
                const auto bytes = read_qnx6_file(image, pin, item.first);
                const auto target = out / item.second;
                need(!std::filesystem::exists(target), "fresh theme output required");
                std::ofstream file(target, std::ios::binary);
                file.write(bytes.data(), bytes.size());
                file.close();
                need(bool(file), "theme output failed");
                std::cout << item.first << " " << bytes.size() << " " << sha256_bytes(bytes)
                          << '\n';
            }

            return 0;
        }

        if (argc == 4 && std::string_view(argv[1]) == "--qinggan11-731-mcu") {
            const auto image = std::filesystem::canonical(argv[2]),
                       out = std::filesystem::canonical(argv[3]);
            const std::string pin =
                    "4e611fab0db36483422a8ce9face07b5848085a21a974e35c4efa20adc812188";

            for (const auto &item : std::array<std::pair<std::string_view, std::string_view>, 2>{
                         {{"lib64/libdataparser.so.0.1", "dataparser-731"},
                          {"lib64/libenginecore.so.0.1", "engine-731"}}}) {
                const auto bytes = read_qnx6_file(image, pin, item.first);
                const auto target = out / item.second;
                need(!std::filesystem::exists(target), "fresh theme output required");
                std::ofstream file(target, std::ios::binary);
                file.write(bytes.data(), bytes.size());
                file.close();
                need(bool(file), "theme output failed");
                std::cout << item.first << " " << bytes.size() << " " << sha256_bytes(bytes)
                          << '\n';
            }

            return 0;
        }

        if (argc == 4 && std::string_view(argv[1]) == "--qinggan11-611-rpcif") {
            const auto image = std::filesystem::canonical(argv[2]),
                       out = std::filesystem::canonical(argv[3]);
            const auto bytes = read_qnx6_file(
                    image, "f93893afd4dccea26099b11fa84a1c03684d9f4279eeca55d8852b71315837e1",
                    "lib64/librpcif.so.0.1");
            const auto target = out / "rpcif-611";
            need(!std::filesystem::exists(target), "fresh rpcd output required");
            std::ofstream file(target, std::ios::binary);
            file.write(bytes.data(), bytes.size());
            file.close();
            need(bool(file), "rpcd output failed");
            std::cout << bytes.size() << " " << sha256_bytes(bytes) << '\n';

            return 0;
        }

        if (argc == 4 && std::string_view(argv[1]) == "--qinggan11-611-rpcd") {
            const auto image = std::filesystem::canonical(argv[2]),
                       out = std::filesystem::canonical(argv[3]);
            const auto bytes = read_qnx6_file(
                    image, "f93893afd4dccea26099b11fa84a1c03684d9f4279eeca55d8852b71315837e1",
                    "usr/bin/rpcd");
            const auto target = out / "rpcd-611";
            need(!std::filesystem::exists(target), "fresh rpcd output required");
            std::ofstream file(target, std::ios::binary);
            file.write(bytes.data(), bytes.size());
            file.close();
            need(bool(file), "rpcd output failed");
            std::cout << bytes.size() << " " << sha256_bytes(bytes) << '\n';

            return 0;
        }

        if (argc == 4 && std::string_view(argv[1]) == "--qinggan11-611-mcu") {
            const auto image = std::filesystem::canonical(argv[2]),
                       out = std::filesystem::canonical(argv[3]);
            const std::string pin =
                    "f93893afd4dccea26099b11fa84a1c03684d9f4279eeca55d8852b71315837e1";

            for (const auto &item : std::array<std::pair<std::string_view, std::string_view>, 2>{
                         {{"lib64/libdataparser.so.0.1", "dataparser-611"},
                          {"lib64/libenginecore.so.0.1", "engine-611"}}}) {
                const auto bytes = read_qnx6_file(image, pin, item.first);
                const auto target = out / item.second;
                need(!std::filesystem::exists(target), "fresh theme output required");
                std::ofstream file(target, std::ios::binary);
                file.write(bytes.data(), bytes.size());
                file.close();
                need(bool(file), "theme output failed");
                std::cout << item.first << " " << bytes.size() << " " << sha256_bytes(bytes)
                          << '\n';
            }

            return 0;
        }

        if (argc == 4 && std::string_view(argv[1]) == "--qinggan11-611-themes") {
            const auto image = std::filesystem::canonical(argv[2]),
                       out = std::filesystem::canonical(argv[3]);
            const std::string pin =
                    "f93893afd4dccea26099b11fa84a1c03684d9f4279eeca55d8852b71315837e1";

            for (const auto &item : std::array<std::pair<std::string_view, std::string_view>, 2>{
                         {{"usr/bin/themmgr", "themmgr-611"},
                          {"lib64/libclusterscene_withoutfaceid.so.0.1", "cluster-scene-611"}}}) {
                const auto bytes = read_qnx6_file(image, pin, item.first);
                const auto target = out / item.second;
                need(!std::filesystem::exists(target), "fresh theme output required");
                std::ofstream file(target, std::ios::binary);
                file.write(bytes.data(), bytes.size());
                file.close();
                need(bool(file), "theme output failed");
                std::cout << item.first << " " << bytes.size() << " " << sha256_bytes(bytes)
                          << '\n';
            }

            return 0;
        }

        if (argc == 6 && std::string_view(argv[1]) == "--system-initialization") {
            const auto config = validate_qnx6_system_initialization(
                    std::filesystem::absolute(argv[2]), argv[3], std::filesystem::absolute(argv[4]),
                    argv[5]);
            std::cout << "Stopped system initialization verified; regular0600 config bytes="
                      << config.size() << " sha256=" << sha256_bytes(config) << '\n';

            return 0;
        }

        if (argc == 3 && std::string_view(argv[1]) == "--passion") {
            const auto image = std::filesystem::absolute(argv[2]);
            const std::string pin =
                    "9e6395255f9bc51d926fd5ab604612d779de93da59ba71832ee580c8534f42ec";
            const auto runtime = read_qnx6_file(image, pin,
                                                "opt/qt/lib/libQt5Quick3DRuntimeRender.so.5.15.15"),
                       asset = read_qnx6_file(image, pin,
                                              "opt/qt/lib/libQt5Quick3DAssetImport.so.5.15.15");
            need(runtime.size() == 1538576 &&
                         sha256_bytes(runtime) ==
                                 "f17716687baf7db9c4cfc5681854e9b8a9c9211952596cacfc5d10abb171aa1a",
                 "Passion runtime-render library SHA");
            need(asset.size() == 80176 &&
                         sha256_bytes(asset) ==
                                 "fa051e6011f8ad1ed6e225b656924f369ccf5732e94c9e9f36cc6a37e6cfdcaa",
                 "Passion asset-import library SHA");
            std::cout
                    << "Passion long Qt library names extracted from original QNX6, exact SHA verified\n";

            return 0;
        }

        if (argc == 2) {
            const auto image = std::filesystem::absolute(argv[1]);
            const std::string pin =
                    "ed32a05382848e0fb63cb33cdf0de55ec3d088f37aad21637b24c12e22a7a6bf";
            const auto screenshot = read_qnx6_file(image, pin, "usr/bin/screenshot");
            need(sha256_bytes(screenshot) ==
                         "8dc5cc5a1dc9cd28682bc32e880894b3fba664db439c401086187a5a4d967561",
                 "OEM screenshot differs");
            std::cout
                    << "OEM screenshot extracted directly from raw QNX6 image, exact SHA verified\n";

            return 0;
        }

        bool failed = false;
        try {
            read_qnx6_file({}, "", "");
        } catch (...) {
            failed = true;
        }

        need(failed, "invalid input accepted");
        std::cout << "QNX6 short/long names, active roots, bounds and checksum guards passed\n";

        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';

        return 1;
    }
}
