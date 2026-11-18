#include "resource_component.hpp"
#include "check.hpp"
#include "error.hpp"
#include "ordinary_path.hpp"
#include "os/fs.hpp"
#include "process.hpp"
#include "quickboot.hpp"
#include "zip_archive.hpp"
#include <algorithm>
#include <fstream>
#include <limits>
#include <map>
#include <set>
namespace voyemu {
namespace {
constexpr Check need{"RESOURCE_COMPONENT"};

void ordinary(const std::filesystem::path &p) {
    ordinary_path(p, need, PathForm::Canonical, "canonical path required", "linked input");
}

std::string read(const std::filesystem::path &p) {
    need(std::filesystem::is_regular_file(p) && std::filesystem::hard_link_count(p) == 1 &&
                 std::filesystem::file_size(p) <= 1ULL << 20,
         "bounded ordinary source required");
    std::ifstream in(p, std::ios::binary);
    need(bool(in), "source open");
    std::string b((std::istreambuf_iterator<char>(in)), {});
    need(!in.bad(), "source read");

    return b;
}

std::string generated_manifest(const std::filesystem::path &apk, const std::string &apk_pin) {
    need(sha256_file(apk) == apk_pin, "linked APK changed");
    const auto manifest = read_pinned_zip_entry(apk, apk_pin, "AndroidManifest.xml", 1 << 20);
    need(manifest.size() >= 8 && sha256_file(apk) == apk_pin, "complete linked manifest");

    return manifest;
}

Json tree(const std::filesystem::path &root) {
    ordinary(root);
    need(std::filesystem::is_directory(root), "source directory required");
    Json result = Json::object();
    std::uint64_t size = 0;

    for (const auto &e : std::filesystem::recursive_directory_iterator(root)) {
        need(!e.is_symlink() && (e.is_directory() || e.is_regular_file()),
             "ordinary source tree required");

        if (e.is_directory()) {
            continue;
        }

        const auto name = e.path().lexically_relative(root).generic_string();
        need(result.size() < 64, "source count");
        auto b = read(e.path());
        size += b.size();
        need(size <= 4ULL << 20, "source byte budget");
        result[name] = {{"bytes", b.size()}, {"sha256", sha256_bytes(b)}};
    }

    return result;
}
} // namespace

void validate_statusbar_overlay_manifest(std::string_view data) {
    auto word = [&](std::size_t at, unsigned n) -> std::uint32_t {
        need(at <= data.size() && n <= data.size() - at, "AXML extent");
        std::uint32_t v = 0;

        for (unsigned i = 0; i < n; ++i) {
            v |= static_cast<std::uint32_t>(static_cast<unsigned char>(data[at + i])) << (i * 8);
        }

        return v;
    };

    need(data.size() >= 8 && data.size() <= 1ULL << 20 && word(0, 2) == 3 && word(2, 2) == 8 &&
                 word(4, 4) == data.size(),
         "AXML header");
    std::vector<std::string> strings, stack;
    unsigned pools = 0, manifest = 0, sdk = 0, overlay = 0, application = 0;
    auto string = [&](std::uint32_t i) -> const std::string & {
        need(i < strings.size(), "AXML string index");

        return strings[i];
    };

    for (std::size_t at = 8; at < data.size();) {
        need(data.size() - at >= 8, "AXML chunk");
        auto type = word(at, 2), header = word(at + 2, 2), size = word(at + 4, 4);
        need(header >= 8 && size >= header && size <= data.size() - at, "AXML chunk bounds");

        if (type == 1) {
            need(++pools == 1 && header == 28 && size >= 28, "AXML string pool");
            auto count = word(at + 8, 4), styles = word(at + 12, 4), flags = word(at + 16, 4),
                 start = word(at + 20, 4);
            need(count <= 4096 && styles == 0 && (flags == 0 || flags == 0x100) &&
                         start >= 28 + count * 4 && start <= size,
                 "AXML string pool bounds");

            for (unsigned i = 0; i < count; ++i) {
                std::size_t cursor = at + start +
                                     word(at + 28 + static_cast<std::size_t>(i) * 4, 4);
                need(cursor < at + size, "AXML string offset");
                std::string s;

                if (flags == 0x100) {
                    auto len = [&]() {
                        auto n = word(cursor++, 1);

                        if (n & 128) {
                            n = ((n & 127) << 8) | word(cursor++, 1);
                        }

                        return n;
                    };
                    auto units = len(), bytes = len();
                    need(units == bytes && bytes <= 1024 && cursor + bytes < at + size,
                         "ASCII pool");

                    for (unsigned j = 0; j < bytes; ++j) {
                        auto c = word(cursor++, 1);
                        need(c > 0 && c < 128, "ASCII pool value");
                        s += static_cast<char>(c);
                    }

                    need(word(cursor, 1) == 0, "AXML string terminator");
                } else {
                    auto len = word(cursor, 2);
                    cursor += 2;
                    need(len <= 1024 && cursor + static_cast<std::size_t>(len) * 2 + 2 <= at + size,
                         "UTF16 pool");

                    for (unsigned j = 0; j < len; ++j) {
                        auto c = word(cursor, 2);
                        cursor += 2;
                        need(c > 0 && c < 128, "ASCII contract");
                        s += static_cast<char>(c);
                    }

                    need(word(cursor, 2) == 0, "AXML string terminator");
                }

                strings.push_back(std::move(s));
            }
        } else if (type == 0x102) {
            need(pools == 1 && header == 16 && size >= 36, "AXML element");
            const auto name = string(word(at + 20, 4));
            auto begin = word(at + 24, 2), stride = word(at + 26, 2), count = word(at + 28, 2);
            need(begin >= 20 && stride == 20 && count <= 16 && 16 + begin + count * 20 <= size,
                 "AXML attrs");
            std::map<std::string, std::pair<unsigned, std::string>> attrs;

            for (unsigned i = 0; i < count; ++i) {
                const auto pos = at + 16 + begin + static_cast<std::size_t>(i) * 20;
                const auto ns = word(pos, 4), kind = word(pos + 15, 1), value = word(pos + 16, 4);
                need(word(pos + 12, 2) == 8 && word(pos + 14, 1) == 0, "AXML typed value");
                need(ns == 0xffffffff || string(ns) == "http://schemas.android.com/apk/res/android",
                     "AXML namespace");
                auto key = (ns == 0xffffffff ? std::string{} : std::string("android:")) +
                           string(word(pos + 4, 4));
                std::string text;

                if (kind == 3) {
                    text = string(value);
                } else if (kind == 0x10) {
                    text = std::to_string(value);
                } else if (kind == 0x12) {
                    text = value ? "true" : "false";
                } else {
                    need(false, "AXML attribute type");
                }

                need(attrs.emplace(key, std::pair<unsigned, std::string>{kind, text}).second,
                     "duplicate attribute");
            }

            const auto parent = stack.empty() ? "" : stack.back();
            std::set<std::string> allowed;
            auto has = [&](const char *key, const char *value, unsigned kind) {
                auto it = attrs.find(key);

                return it != attrs.end() && it->second.first == kind && it->second.second == value;
            };

            if (name == "manifest") {
                allowed = {"package", "android:compileSdkVersion",
                           "android:compileSdkVersionCodename", "platformBuildVersionCode",
                           "platformBuildVersionName"};
                need(parent.empty() && ++manifest == 1 &&
                             has("package", "com.voyemu.output.overlay.statusbar", 3),
                     "manifest identity");
            } else if (name == "uses-sdk") {
                allowed = {"android:minSdkVersion", "android:targetSdkVersion"};
                need(parent == "manifest" && ++sdk == 1 &&
                             has("android:minSdkVersion", "28", 0x10) &&
                             has("android:targetSdkVersion", "28", 0x10),
                     "overlay SDK");
            } else if (name == "overlay") {
                allowed = {"android:targetPackage", "android:isStatic", "android:priority"};
                need(parent == "manifest" && ++overlay == 1 &&
                             has("android:targetPackage", "android", 3) &&
                             has("android:isStatic", "false", 0x12) &&
                             has("android:priority", "999", 0x10),
                     "overlay declaration");
            } else if (name == "application") {
                allowed = {"android:hasCode"};
                need(parent == "manifest" && ++application == 1 &&
                             has("android:hasCode", "false", 0x12),
                     "resource-only application");
            } else {
                need(false, "unexpected manifest element");
            }

            for (const auto &[key, value] : attrs) {
                (void)value;
                need(allowed.contains(key), "unexpected manifest attribute");
            }

            stack.push_back(name);
        } else if (type == 0x103) {
            need(header == 16 && size >= 24 && !stack.empty() &&
                         string(word(at + 20, 4)) == stack.back(),
                 "AXML closing element");
            stack.pop_back();
        } else {
            need(type == 0x100 || type == 0x101 || type == 0x180, "unsupported AXML chunk");
        }

        at += size;
    }

    need(pools == 1 && manifest == 1 && sdk == 1 && overlay == 1 && application == 1 &&
                 stack.empty(),
         "complete overlay manifest required");
}

Json build_resource_component(const std::filesystem::path &source,
                              const std::filesystem::path &aapt2, const std::string &aapt2_pin,
                              const std::filesystem::path &framework,
                              const std::string &framework_pin,
                              const std::filesystem::path &destination, std::uint64_t reserve) {
    const auto sources = tree(source);

    need(sources.contains("component.json") && sources.contains("AndroidManifest.xml") &&
                 sources.contains("res/values/dimens.xml") &&
                 sources.contains("native-sources.json"),
         "materialized overlay inputs required");
    const auto definition = read_private_json(source / "component.json");
    need(definition ==
                 Json({{"type", "apk"}, {"minSdk", 28}, {"targetSdk", 28}, {"sign", "platform"}}),
         "registered resource-only recipe required");
    const auto materialized = read_private_json(source / "native-sources.json");
    auto payload = sources;
    payload.erase("native-sources.json");
    need(materialized.value("schema", 0) == 1 && materialized.value("native", false) &&
                 materialized.at("files") == payload.size() &&
                 materialized.at("definition_sha256") ==
                         sources.at("component.json").at("sha256") &&
                 materialized.at("source_tree_sha256") == sha256_bytes(payload.dump()),
         "source receipt mismatch");
    const std::set<std::string> allowed = {"component.json",        "AndroidManifest.xml",
                                           "res/values/dimens.xml", "README.md",
                                           "README.en.md",          "native-sources.json"};

    for (auto it = sources.begin(); it != sources.end(); ++it) {
        need(allowed.contains(it.key()), "undeclared resource or Java source");
    }

    const auto manifest = read(source / "AndroidManifest.xml"),
               dimensions = read(source / "res/values/dimens.xml");
    need(manifest.find("package=\"com.voyemu.output.overlay.statusbar\"") != std::string::npos &&
                 manifest.find("android:hasCode=\"false\"") != std::string::npos,
         "source manifest binding");
    need(dimensions ==
                 "<resources>\n    <dimen name=\"status_bar_height\">0dp</dimen>\n    <dimen name=\"status_bar_height_portrait\">0dp</dimen>\n    <dimen name=\"status_bar_height_landscape\">0dp</dimen>\n</resources>\n",
         "registered statusbar resources required");
    ordinary(aapt2);
    ordinary(framework);
    need(sha256_text(aapt2_pin) && sha256_text(framework_pin) &&
                 std::filesystem::is_regular_file(aapt2) &&
                 std::filesystem::is_regular_file(framework) && sha256_file(aapt2) == aapt2_pin &&
                 sha256_file(framework) == framework_pin,
         "pinned native aapt2/framework required");
    const auto tool_epoch = cache_file_identity(aapt2),
               framework_epoch = cache_file_identity(framework);
    need(destination.is_absolute() && destination.lexically_normal() == destination &&
                 !std::filesystem::exists(destination) && !std::filesystem::is_symlink(destination),
         "fresh output directory required");
    auto existing = destination.parent_path();

    while (!std::filesystem::exists(existing)) {
        existing = existing.parent_path();
    }

    need(reserve <= std::numeric_limits<std::uint64_t>::max() - (64ULL << 20) &&
                 std::filesystem::space(existing).available >= reserve + (64ULL << 20),
         "disk reserve");
    ensure_private_directory(destination.parent_path());
    need(std::filesystem::create_directory(destination), "exclusive output directory");
    const auto token = random_token();
    write_private_json(destination / ".owner.json", {{"token", token}});
    struct Cleanup {
        std::filesystem::path p;
        std::string token;
        bool published = false;

        ~Cleanup() {
            if (published) {
                return;
            }

            try {
                if (read_private_json(p / ".owner.json").at("token") == token) {
                    std::filesystem::remove_all(p);
                }
            } catch (...) { /* Ignored: cleanup of owned temporary state is best-effort. */
            }
        }
    } cleanup{destination, token};
    ProcessSpec cmd;
    cmd.executable = aapt2;
    cmd.directory = destination;
    cmd.environment = {{"JAVA_HOME", "/nonexistent"},
                       {"NODE_PATH", "/nonexistent"},
                       {"TMPDIR", destination.string()},
                       {"TMP", destination.string()},
                       {"TEMP", destination.string()}};
    cmd.output = destination / "compile.log";
    cmd.arguments = {"compile", "--dir", (source / "res").string(), "-o",
                     (destination / "compiled.zip").string()};
    const auto compile = execute(cmd, 120000, 128 * 1024);
    need(!compile.timed_out && compile.exit_code == 0, "native resource compile failed");
    cmd.output = destination / "link.log";
    cmd.arguments = {"link",
                     "-I",
                     framework.string(),
                     "--manifest",
                     (source / "AndroidManifest.xml").string(),
                     "--min-sdk-version",
                     "28",
                     "--target-sdk-version",
                     "28",
                     "--auto-add-overlay",
                     "-o",
                     (destination / "unsigned.apk").string(),
                     (destination / "compiled.zip").string()};
    const auto link = execute(cmd, 120000, 128 * 1024);
    need(!link.timed_out && link.exit_code == 0, "native resource link failed");
    const auto apk = destination / "unsigned.apk";
    need(std::filesystem::is_regular_file(apk) && !std::filesystem::is_symlink(apk) &&
                 std::filesystem::file_size(apk) <= 16ULL << 20,
         "bounded linked APK required");
    const auto apk_pin = sha256_file(apk);
    bool has_manifest = false, has_resources = false;
    std::string manifest_pin;

    for (const auto &e : pinned_zip_inventory(apk, apk_pin)) {
        if (e.directory) {
            continue;
        }

        need(e.name == "AndroidManifest.xml" || e.name == "resources.arsc",
             "resource-only APK entry required");

        if (e.name == "AndroidManifest.xml") {
            need(!has_manifest, "duplicate manifest");
            has_manifest = true;
        } else {
            need(!has_resources, "duplicate resources");
            has_resources = true;
        }
    }

    need(has_manifest && has_resources, "manifest/resources required");
    const auto linked_manifest = generated_manifest(apk, apk_pin);
    manifest_pin = sha256_bytes(linked_manifest);
    validate_statusbar_overlay_manifest(linked_manifest);
    need(tree(source) == sources && cache_file_identity(aapt2) == tool_epoch &&
                 cache_file_identity(framework) == framework_epoch &&
                 sha256_file(aapt2) == aapt2_pin && sha256_file(framework) == framework_pin,
         "source/tool/framework changed during build");
    const Json result = {{"schema", 1},
                         {"type", "resource-only-apk"},
                         {"component", "statusbar-overlay"},
                         {"api", 28},
                         {"output", "unsigned.apk"},
                         {"sha256", apk_pin},
                         {"bytes", std::filesystem::file_size(apk)},
                         {"manifest_sha256", manifest_pin},
                         {"source_tree_sha256", sha256_bytes(payload.dump())},
                         {"sources", sources},
                         {"aapt2_sha256", aapt2_pin},
                         {"framework_sha256", framework_pin},
                         {"materialized_source", materialized},
                         {"dex", false},
                         {"signed", false},
                         {"host_java_required", false},
                         {"host_node_required", false},
                         {"host_python_required", false}};
    write_private_json(destination / "receipt.json", result);
    need(std::filesystem::remove(destination / "compiled.zip"), "compiled intermediate cleanup");
    std::filesystem::remove(destination / ".owner.json");
    cleanup.published = true;

    return result;
}
} // namespace voyemu
