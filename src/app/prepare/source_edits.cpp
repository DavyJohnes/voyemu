#include "source_edits.hpp"
#include "check.hpp"
#include "error.hpp"
#include "os/fs.hpp"
#include "process.hpp"
#include "quickboot.hpp"
#include <algorithm>
#include <fstream>
#include <map>
#include <set>
namespace voyemu {
namespace {
[[noreturn]] void bad(const char *reason) {
    throw VoyemuError("SOURCE_EDITS", reason);
}

std::size_t integer(const Json &value, std::size_t minimum) {
    if (!value.is_number_integer() || value.is_number_float() ||
        value.get<std::int64_t>() < static_cast<std::int64_t>(minimum) ||
        value.get<std::uint64_t>() > 10000000) {
        bad("bounded integer required");
    }

    return value.get<std::size_t>();
}

std::string path(const std::string &value) {
    if (value.empty() || value.size() > 1024 || value.front() == '/' ||
        value.find_first_of("\\:\r\n\t") != std::string::npos ||
        value.find("//") != std::string::npos) {
        bad("ordinary relative path required");
    }

    const std::filesystem::path candidate(std::u8string(value.begin(), value.end()));

    if (candidate.lexically_normal() != candidate) {
        bad("noncanonical path");
    }

    for (const auto &item : candidate) {
        if (item == ".." || item == "." || item.empty()) {
            bad("path traversal");
        }
    }

    return value;
}

std::string pin(const Json &value) {
    const auto sha = value.get<std::string>();

    if (!sha256_text(sha)) {
        bad("exact lowercase SHA256 required");
    }

    return sha;
}

std::string read(const std::filesystem::path &file, std::size_t maximum = 1024 * 1024) {
    for (const auto &at : os::ancestors(file)) {
        if (std::filesystem::is_symlink(std::filesystem::symlink_status(at))) {
            bad("linked source path");
        }
    }

    if (!std::filesystem::is_regular_file(file) || std::filesystem::hard_link_count(file) != 1 ||
        std::filesystem::file_size(file) > maximum) {
        bad("ordinary bounded source required");
    }

    const auto epoch = cache_file_identity(file);
    std::ifstream stream(file, std::ios::binary);

    if (!stream) {
        bad("source open failed");
    }

    std::string result((std::istreambuf_iterator<char>(stream)), {});

    if (result.size() != std::filesystem::file_size(file) || result.size() > maximum ||
        cache_file_identity(file) != epoch) {
        bad("source changed while reading");
    }

    return result;
}
} // namespace

std::string assemble_source_edits(std::string_view original, const Json &edits) {
    if (original.size() > 1024 * 1024 || !edits.is_array() || edits.size() > 4096) {
        bad("bounded source and edit list required");
    }

    std::vector<std::size_t> offsets{0};

    for (std::size_t at = 0; at < original.size(); ++at) {
        if (original[at] == '\n') {
            offsets.push_back(at + 1);
        }
    }

    const auto lines = offsets.size() - 1 + (!original.empty() && original.back() != '\n' ? 1 : 0);
    auto begin = [&](std::size_t line) {
        return line <= offsets.size() ? offsets[line - 1] : original.size();
    };
    std::vector<Json> sorted(edits.begin(), edits.end());
    std::sort(sorted.begin(), sorted.end(), [](const Json &a, const Json &b) {
        return integer(a.at("startLine"), 1) < integer(b.at("startLine"), 1);
    });
    std::size_t previous = 0, occupied = 0;
    std::string output;
    std::size_t cursor = 0;
    auto append = [&](std::string_view bytes) {
        if (bytes.size() > 4 * 1024 * 1024 - output.size()) {
            bad("assembled source exceeds bound");
        }

        output += bytes;
    };

    for (const auto &edit : sorted) {
        if (!edit.is_object() || edit.size() != 3 || !edit.contains("startLine") ||
            !edit.contains("deleteCount") || !edit.contains("insert") ||
            !edit.at("insert").is_array()) {
            bad("exact edit schema required");
        }

        const auto start = integer(edit.at("startLine"), 1),
                   removed = integer(edit.at("deleteCount"), 0);

        if (start > lines + 1 || removed > lines + 1 - start || start == previous ||
            start < occupied) {
            bad("overlapping or out-of-range edits");
        }

        append(original.substr(cursor, begin(start) - cursor));

        for (const auto &segment : edit.at("insert")) {
            if (!segment.is_object() || segment.size() != 1) {
                bad("exact segment schema required");
            }

            if (segment.contains("text")) {
                if (!segment.at("text").is_string()) {
                    bad("text segment required");
                }

                append(segment.at("text").get<std::string>());
            } else if (segment.contains("copy")) {
                const auto &copy = segment.at("copy");

                if (!copy.is_object() || copy.size() != 2 || !copy.contains("startLine") ||
                    !copy.contains("lineCount")) {
                    bad("exact copy schema required");
                }

                const auto first = integer(copy.at("startLine"), 1),
                           count = integer(copy.at("lineCount"), 1);

                if (first > lines || count > lines + 1 - first) {
                    bad("copy range outside original");
                }

                append(original.substr(begin(first), begin(first + count) - begin(first)));
            } else {
                bad("unknown insertion segment");
            }
        }

        cursor = begin(start + removed);
        previous = start;
        occupied = start + removed;
    }

    append(original.substr(cursor));

    return output;
}

Json materialize_native_component_sources(const std::filesystem::path &source,
                                          const std::filesystem::path &cache,
                                          const std::filesystem::path &destination) {
    if (!source.is_absolute() || !cache.is_absolute() || !destination.is_absolute() ||
        source.lexically_normal() != source || destination.lexically_normal() != destination ||
        std::filesystem::exists(destination) || std::filesystem::is_symlink(destination)) {
        bad("absolute inputs and fresh destination required");
    }

    const auto definition = read_private_json(source / "component.json");
    const auto external = definition.value("externalSources", Json::object());

    if (!external.is_object() || external.size() > 1024) {
        bad("bounded external sources required");
    }

    std::map<std::string, std::string> payloads;
    std::map<std::filesystem::path, Json> epochs;
    std::set<std::string> locals;
    std::uint64_t total = 0;

    for (const auto &item : std::filesystem::recursive_directory_iterator(source)) {
        if (item.is_symlink() || (!item.is_directory() && !item.is_regular_file())) {
            bad("ordinary local source tree required");
        }

        if (!item.is_regular_file()) {
            continue;
        }

        const auto relative = path(item.path().lexically_relative(source).generic_string());

        if (payloads.size() >= 4096) {
            bad("local source count exceeds maximum");
        }

        epochs[item.path()] = cache_file_identity(item.path());
        locals.insert(relative);
        auto bytes = read(item.path());
        total += bytes.size();

        if (total > 32 * 1024 * 1024) {
            bad("local source tree exceeds maximum");
        }

        payloads.emplace(relative, std::move(bytes));
    }

    Json upstream = Json::object();

    for (auto it = external.begin(); it != external.end(); ++it) {
        const auto relative = path(it.key()), sha = pin(it.value().at("sha256"));

        if (payloads.contains(relative)) {
            bad("external input collides with local source");
        }

        const auto file = cache / sha;
        epochs[file] = cache_file_identity(file);
        auto bytes = read(file);

        if (sha256_bytes(bytes) != sha) {
            bad("external snapshot differs from pin");
        }

        total += bytes.size();

        if (total > 32 * 1024 * 1024) {
            bad("materialized tree exceeds maximum");
        }

        upstream[relative] = sha;
        payloads.emplace(relative, std::move(bytes));
    }

    if (definition.contains("upstreamEdits")) {
        const auto name = path(definition.at("upstreamEdits").get<std::string>());

        if (!payloads.contains(name)) {
            bad("edit description missing");
        }

        const auto description = Json::parse(payloads.at(name));

        if (!description.is_object() || description.size() != 2 || description.at("version") != 1 ||
            !description.at("files").is_array() || description.at("files").size() > 1024) {
            bad("version-1 source edits required");
        }

        std::set<std::string> edited;

        for (const auto &file : description.at("files")) {
            if (!file.is_object() || file.size() != 3) {
                bad("exact file edit schema required");
            }

            const auto relative = path(file.at("path").get<std::string>());

            if (!upstream.contains(relative) || !edited.insert(relative).second) {
                bad("only one edit set per declared external source");
            }

            auto bytes = assemble_source_edits(payloads.at(relative), file.at("edits"));

            if (sha256_bytes(bytes) != pin(file.at("resultSha256"))) {
                bad("assembled source differs from result pin");
            }

            payloads[relative] = std::move(bytes);
        }
    }

    std::set<std::string> folded;
    total = 0;

    for (const auto &[relative, bytes] : payloads) {
        auto key = relative;

        for (char &c : key) {
            if (c >= 'A' && c <= 'Z') {
                c += 32;
            }
        }

        if (!folded.insert(key).second) {
            bad("case-aliased source path");
        }

        total += bytes.size();

        if (total > 32 * 1024 * 1024) {
            bad("edited source tree exceeds maximum");
        }
    }

    for (const auto &[relative, bytes] : payloads) {
        (void)bytes;

        for (auto parent = std::filesystem::path(relative).parent_path(); !parent.empty();
             parent = parent.parent_path()) {
            auto key = parent.generic_string();

            for (char &c : key) {
                if (c >= 'A' && c <= 'Z') {
                    c += 32;
                }
            }

            if (folded.contains(key)) {
                bad("source file used as directory");
            }
        }
    }

    ensure_private_directory(destination.parent_path());

    if (!std::filesystem::create_directory(destination)) {
        bad("exclusive source destination failed");
    }

    const auto token = random_token();
    write_private_json(destination / ".owner.json", {{"token", token}});
    struct Cleanup {
        std::filesystem::path path;
        std::string token;
        bool published = false;

        ~Cleanup() {
            if (published) {
                return;
            }

            try {
                if (read_private_json(path / ".owner.json").at("token") == token) {
                    std::filesystem::remove_all(path);
                }
            } catch (...) { /* Ignored: cleanup of owned temporary state is best-effort. */
            }
        }
    } cleanup{destination, token};
    Json files = Json::object();

    for (const auto &[relative, bytes] : payloads) {
        const auto output = destination / relative;
        ensure_private_directory(output.parent_path());
        std::ofstream stream(output, std::ios::binary | std::ios::out);
        stream.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        stream.close();
        const auto hash = sha256_bytes(bytes);

        if (!stream || sha256_file(output) != hash) {
            bad("incomplete source publication");
        }

        files[relative] = {{"sha256", hash}, {"bytes", bytes.size()}};
    }

    for (const auto &[file, epoch] : epochs) {
        if (cache_file_identity(file) != epoch) {
            bad("source or cache changed during materialization");
        }
    }

    std::set<std::string> current;

    for (const auto &item : std::filesystem::recursive_directory_iterator(source)) {
        if (item.is_symlink() || (!item.is_directory() && !item.is_regular_file())) {
            bad("source tree changed type");
        }

        if (item.is_regular_file()) {
            current.insert(item.path().lexically_relative(source).generic_string());
        }
    }

    if (current != locals) {
        bad("source inventory changed during materialization");
    }

    const Json receipt = {{"schema", 1},
                          {"definition_sha256", sha256_bytes(payloads.at("component.json"))},
                          {"external_sources", upstream.size()},
                          {"external_tree_sha256", sha256_bytes(upstream.dump())},
                          {"files", files.size()},
                          {"source_tree_sha256", sha256_bytes(files.dump())},
                          {"native", true}};
    write_private_json(destination / "native-sources.json", receipt);
    std::filesystem::remove(destination / ".owner.json");
    cleanup.published = true;

    return {{"files", files.size()},
            {"external_sources", upstream.size()},
            {"source_tree_sha256", sha256_bytes(files.dump())},
            {"native", true}};
}
} // namespace voyemu
