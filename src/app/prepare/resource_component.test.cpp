#include "resource_component.hpp"
#include "model.hpp"
#include "process.hpp"
#include <fstream>
#include <iostream>
#include <map>
#include <tuple>
using namespace voyemu;
namespace {
void check(bool value) {
    if (!value) {
        throw std::runtime_error("resource component test failed");
    }
}

void put(std::string &b, std::size_t at, std::uint32_t value, unsigned n = 4) {
    if (b.size() < at + n) {
        b.resize(at + n);
    }

    for (unsigned i = 0; i < n; ++i) {
        b[at + i] = static_cast<char>(value >> (i * 8));
    }
}

struct Xml {
    std::vector<std::string> strings = {"http://schemas.android.com/apk/res/android",
                                        "manifest",
                                        "package",
                                        "com.voyemu.output.overlay.statusbar",
                                        "uses-sdk",
                                        "minSdkVersion",
                                        "targetSdkVersion",
                                        "overlay",
                                        "targetPackage",
                                        "android",
                                        "isStatic",
                                        "priority",
                                        "application",
                                        "hasCode"};
    std::string body;

    unsigned id(const std::string &value) {
        for (unsigned i = 0; i < strings.size(); ++i) {
            if (strings[i] == value) {
                return i;
            }
        }

        throw std::runtime_error("fixture string");
    }

    void start(const std::string &name,
               const std::vector<std::tuple<std::string, unsigned, unsigned>> &attrs) {
        std::string b(36 + attrs.size() * 20, '\0');
        put(b, 0, 0x102, 2);
        put(b, 2, 16, 2);
        put(b, 4, b.size());
        put(b, 12, 0xffffffff);
        put(b, 16, 0xffffffff);
        put(b, 20, id(name));
        put(b, 24, 20, 2);
        put(b, 26, 20, 2);
        put(b, 28, attrs.size(), 2);

        for (unsigned i = 0; i < attrs.size(); ++i) {
            const auto &[key, kind, value] = attrs[i];
            const auto at = 36 + i * 20;
            put(b, at, key == "package" ? 0xffffffff : 0);
            put(b, at + 4, id(key));
            put(b, at + 8, 0xffffffff);
            put(b, at + 12, 8, 2);
            put(b, at + 15, kind, 1);
            put(b, at + 16, value);
        }

        body += b;
    }

    void end(const std::string &name) {
        std::string b(24, '\0');
        put(b, 0, 0x103, 2);
        put(b, 2, 16, 2);
        put(b, 4, 24);
        put(b, 12, 0xffffffff);
        put(b, 16, 0xffffffff);
        put(b, 20, id(name));
        body += b;
    }

    std::string document() {
        std::string pool(28 + strings.size() * 4, '\0');
        put(pool, 0, 1, 2);
        put(pool, 2, 28, 2);
        put(pool, 8, strings.size());
        put(pool, 16, 0x100);
        put(pool, 20, pool.size());
        const auto begin = pool.size();

        for (unsigned i = 0; i < strings.size(); ++i) {
            put(pool, 28 + i * 4, pool.size() - begin);
            pool.push_back(static_cast<char>(strings[i].size()));
            pool.push_back(static_cast<char>(strings[i].size()));
            pool += strings[i];
            pool.push_back(0);
        }

        while (pool.size() % 4) {
            pool.push_back(0);
        }

        put(pool, 4, pool.size());
        std::string result(8, '\0');
        put(result, 0, 3, 2);
        put(result, 2, 8, 2);
        result += pool + body;
        put(result, 4, result.size());

        return result;
    }
};

std::string manifest(bool code = false, bool duplicate = false, bool wrong_target = false) {
    Xml x;

    x.start("manifest", {{"package", 3, x.id("com.voyemu.output.overlay.statusbar")}});
    x.start("uses-sdk", {{"minSdkVersion", 0x10, 28}, {"targetSdkVersion", 0x10, 28}});
    x.end("uses-sdk");
    x.start("overlay", {{"targetPackage", 3, x.id(wrong_target ? "manifest" : "android")},
                        {"isStatic", 0x12, 0},
                        {"priority", 0x10, 999}});
    x.end("overlay");
    x.start("application", {{"hasCode", 0x12, code ? 1U : 0U}});
    x.end("application");

    if (duplicate) {
        x.start("application", {{"hasCode", 0x12, 0}});
        x.end("application");
    }

    x.end("manifest");

    return x.document();
}

void receipt(const std::filesystem::path &root) {
    Json files = Json::object();

    for (const auto &e : std::filesystem::recursive_directory_iterator(root)) {
        if (e.is_regular_file() && e.path().filename() != "native-sources.json") {
            files[e.path().lexically_relative(root).generic_string()] = {
                    {"bytes", e.file_size()}, {"sha256", sha256_file(e.path())}};
        }
    }

    write_private_json(root / "native-sources.json",
                       {{"schema", 1},
                        {"native", true},
                        {"files", files.size()},
                        {"definition_sha256", sha256_file(root / "component.json")},
                        {"source_tree_sha256", sha256_bytes(files.dump())}});
}
} // namespace

int main(int argc, char **argv) {
    try {
        if (argc == 8 && std::string(argv[1]) == "--build") {
            const auto result = build_resource_component(
                    std::filesystem::absolute(argv[2]), std::filesystem::absolute(argv[3]), argv[4],
                    std::filesystem::absolute(argv[5]), argv[6], std::filesystem::absolute(argv[7]),
                    0);
            std::cout << result.dump() << '\n';

            return 0;
        }

        check(argc == 1);
        validate_statusbar_overlay_manifest(manifest());

        for (const auto &bad : {manifest(true), manifest(false, true), manifest(false, false, true),
                                std::string("bad")}) {
            bool rejected = false;
            try {
                validate_statusbar_overlay_manifest(bad);
            } catch (const std::exception &) {
                rejected = true;
            }

            check(rejected);
        }

        const auto root = std::filesystem::canonical(std::filesystem::temp_directory_path()) /
                          ("voyemu-resource-test-" + random_token());
        ensure_private_directory(root);
        struct Cleanup {
            std::filesystem::path p;

            ~Cleanup() {
                std::error_code e;
                std::filesystem::remove_all(p, e);
            }
        } cleanup{root};
        const auto source = root / "source";
        ensure_private_directory(source / "res/values");
        const auto original =
                std::filesystem::path(__FILE__)
                        .parent_path()
                        .parent_path()
                        .parent_path()
                        .parent_path() /
                "profiles/free-09-chinese-20260106-6.0.3/components/statusbar-overlay";

        for (const auto &name :
             {"component.json", "AndroidManifest.xml", "res/values/dimens.xml"}) {
            std::filesystem::copy_file(original / name, source / name);
        }

        auto refuse = [&] {
            bool rejected = false;
            try {
                build_resource_component(source, root / "missing-aapt2", std::string(64, '0'),
                                         root / "missing-framework", std::string(64, '0'),
                                         root / "output", 0);
            } catch (const std::exception &) {
                rejected = true;
            }

            check(rejected && !std::filesystem::exists(root / "output"));
        };
        refuse();
        receipt(source);
        refuse();
        std::ofstream(source / "res/values/dimens.xml", std::ios::app) << " ";
        refuse();
        receipt(source);
        refuse();
        std::filesystem::copy_file(original / "res/values/dimens.xml",
                                   source / "res/values/dimens.xml",
                                   std::filesystem::copy_options::overwrite_existing);
        std::ofstream(source / "Extra.java") << "class Extra {}\n";
        receipt(source);
        refuse();
        std::filesystem::remove(source / "Extra.java");
        write_private_json(
                source / "component.json",
                {{"type", "apk"}, {"minSdk", 28}, {"targetSdk", 29}, {"sign", "platform"}});
        receipt(source);
        refuse();
        std::cout << "resource component manifest/source guards passed\n";

        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';

        return 1;
    }
}
