#include "resource_apk.hpp"
#include "apk_alignment.hpp"
#include "apk_base.hpp"
#include "apk_crypto.hpp"
#include "apk_sign.hpp"
#include "check.hpp"
#include "error.hpp"
#include "os/fs.hpp"
#include "process.hpp"
#include "quickboot.hpp"
#include "resource_component.hpp"
#include <limits>
#ifdef __APPLE__
#include <stdio.h>
#elif defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
namespace voyemu {
namespace {
constexpr Check need{"RESOURCE_APK"};

void ordinary_path(const std::filesystem::path &path) {
    need(path.is_absolute() && path.lexically_normal() == path &&
                 std::filesystem::canonical(path) == path,
         "canonical input required");

    for (const auto &at : os::ancestors(path)) {
        need(!std::filesystem::is_symlink(std::filesystem::symlink_status(at)), "linked input");
#ifdef _WIN32
        const auto attributes = GetFileAttributesW(at.c_str());
        need(attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_REPARSE_POINT),
             "reparse input");
#endif
    }
}

Json source_snapshot(const std::filesystem::path &root) {
    ordinary_path(root);
    need(std::filesystem::is_directory(root), "source directory required");
    Json files = Json::object();
    std::uint64_t bytes = 0;

    for (const auto &entry : std::filesystem::recursive_directory_iterator(root)) {
        ordinary_path(entry.path());
        need(entry.is_directory() || entry.is_regular_file(), "ordinary source tree required");

        if (entry.is_directory()) {
            continue;
        }

        need(files.size() < 64 && std::filesystem::hard_link_count(entry.path()) == 1 &&
                     entry.file_size() <= (1ULL << 20),
             "bounded unaliased source required");
        bytes += entry.file_size();
        need(bytes <= (4ULL << 20), "source byte budget");
        files[entry.path().lexically_relative(root).generic_string()] = {
                {"bytes", entry.file_size()}, {"sha256", sha256_file(entry.path())}};
    }

    return files;
}

Json input_identity(const std::filesystem::path &path, const std::string &sha) {
    need(sha256_text(sha), "lowercase input SHA256 required");
    ordinary_path(path);
    need(std::filesystem::is_regular_file(path) && std::filesystem::hard_link_count(path) == 1,
         "ordinary unaliased input required");
    const auto epoch = cache_file_identity(path);
    need(sha256_file(path) == sha && cache_file_identity(path) == epoch, "input pin mismatch");

    return epoch;
}
} // namespace

Json publish_resource_apk(const ResourceApkRequest &request,
                          const std::filesystem::path &destination, std::uint64_t reserve) {
    need(destination.is_absolute() && destination.lexically_normal() == destination &&
                 !destination.filename().empty() && destination.filename() != "." &&
                 destination.filename() != ".." && !std::filesystem::exists(destination) &&
                 !std::filesystem::is_symlink(std::filesystem::symlink_status(destination)),
         "fresh absolute output directory required");
    need(sha256_text(request.base_certificate_sha256) &&
                 request.identity.certificate_sha256 == request.base_certificate_sha256,
         "selected base certificate mismatch");
    // Reject invalid or mismatched key material before running aapt2 or creating output.
    sign_apk_rsa_sha256(request.identity.pkcs8, request.identity.key_sha256,
                        request.identity.certificate_der, request.identity.certificate_sha256,
                        "voyemu resource APK identity check");
    const auto sources = source_snapshot(request.source);
    const auto aapt2_epoch = input_identity(request.aapt2, request.aapt2_sha256);
    const auto framework_epoch = input_identity(request.framework, request.framework_sha256);
    const auto zipalign_epoch = input_identity(request.zipalign, request.zipalign_sha256);
    need(sha256_bytes(pinned_apk_v2_certificate(request.framework, request.framework_sha256)) ==
                 request.base_certificate_sha256,
         "framework signer differs from selected base");
    auto ancestor = destination.parent_path();

    while (!std::filesystem::exists(ancestor) && ancestor != ancestor.parent_path()) {
        ancestor = ancestor.parent_path();
    }

    ordinary_path(ancestor);
    need(reserve <= std::numeric_limits<std::uint64_t>::max() - (128ULL << 20) &&
                 std::filesystem::space(ancestor).available >= reserve + (128ULL << 20),
         "disk reserve");
    ensure_private_directory(destination.parent_path());
    ordinary_path(destination.parent_path());
    const auto pending = destination.parent_path() /
                         ("." + destination.filename().string() + ".pending-" + random_token());
    need(std::filesystem::create_directory(pending), "exclusive staging directory required");
    ensure_private_directory(pending);
    const auto token = random_token();
    write_private_json(pending / ".owner.json", {{"token", token}});
    struct Cleanup {
        std::filesystem::path path;
        std::string token;
        bool published = false;

        ~Cleanup() {
            if (published) {
                return;
            }

            try {
                ordinary_path(path);

                if (read_private_json(path / ".owner.json").at("token") == token) {
                    std::filesystem::remove_all(path);
                }
            } catch (...) { /* Ignored: cleanup of owned temporary state is best-effort. */
            }
        }
    } cleanup{pending, token};
    const auto unsigned_directory = pending / "unsigned";
    const auto unsigned_receipt = build_resource_component(
            request.source, request.aapt2, request.aapt2_sha256, request.framework,
            request.framework_sha256, unsigned_directory, reserve);
    const auto unsigned_apk = unsigned_directory / "unsigned.apk";
    need(unsigned_receipt.at("output") == "unsigned.apk" &&
                 unsigned_receipt.at("signed") == false && unsigned_receipt.at("dex") == false &&
                 unsigned_receipt.at("component") == "statusbar-overlay",
         "registered unsigned producer receipt required");
    const auto unsigned_sha = unsigned_receipt.at("sha256").get<std::string>();
    const auto aligned = pending / "aligned.apk";
    const auto aligned_sha = align_pinned_apk(unsigned_apk, unsigned_sha, request.zipalign,
                                              request.zipalign_sha256, aligned, reserve);
    const auto signed_apk = pending / "app.apk";
    const auto signed_sha =
            sign_pinned_apk_v2(aligned, aligned_sha, request.identity.pkcs8,
                               request.identity.key_sha256, request.identity.certificate_der,
                               request.identity.certificate_sha256, signed_apk, reserve);
    verify_pinned_apk_alignment(signed_apk, signed_sha);

    if (request.prepublication_guard) {
        request.prepublication_guard();
    }

    need(source_snapshot(request.source) == sources &&
                 input_identity(request.aapt2, request.aapt2_sha256) == aapt2_epoch &&
                 input_identity(request.framework, request.framework_sha256) == framework_epoch &&
                 input_identity(request.zipalign, request.zipalign_sha256) == zipalign_epoch &&
                 sha256_file(signed_apk) == signed_sha,
         "source/tool/output changed during production");
    const Json result = {{"schema", 1},
                         {"kind", "native-resource-apk"},
                         {"component", "statusbar-overlay"},
                         {"api", 28},
                         {"apk", "app.apk"},
                         {"sha256", signed_sha},
                         {"bytes", std::filesystem::file_size(signed_apk)},
                         {"signature", "APK-v2-RSA-SHA256"},
                         {"certificate_sha256", request.base_certificate_sha256},
                         {"framework_sha256", request.framework_sha256},
                         {"aapt2_sha256", request.aapt2_sha256},
                         {"zipalign_sha256", request.zipalign_sha256},
                         {"aligned_sha256", aligned_sha},
                         {"unsigned_provenance", unsigned_receipt},
                         {"dex", false},
                         {"host_java_required", false},
                         {"host_node_required", false},
                         {"host_python_required", false}};
    write_private_json(pending / "receipt.json", result);
    need(std::filesystem::remove(aligned), "aligned intermediate cleanup failed");
    ordinary_path(unsigned_directory);
    need(read_private_json(unsigned_directory / "receipt.json") == unsigned_receipt,
         "unsigned producer directory changed");
    need(std::filesystem::remove_all(unsigned_directory) > 0,
         "unsigned intermediate cleanup failed");
    // Keep ownership marker until the exclusive rename succeeds for failure cleanup.
    need(os::rename_exclusive(pending, destination), "exclusive publication failed");
    cleanup.published = true;

    // The marker remains provenance of directory ownership, never key material.
    return result;
}
} // namespace voyemu
