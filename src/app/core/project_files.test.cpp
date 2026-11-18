#include "project_files.hpp"
#include "java_toolchain.hpp"
#include "process.hpp"
#include "toolchain_manifest.hpp"
#include <algorithm>
#include <fstream>
#include <functional>
#include <iostream>
using namespace voyemu;

namespace {
void check(bool value, const char *message) {
    if (!value) {
        throw std::runtime_error(message);
    }
}

std::string failure(const std::function<void()> &action) {
    try {
        action();
    } catch (const std::exception &error) {
        return error.what();
    }

    return {};
}

void put(const std::filesystem::path &file, const std::string &bytes) {
    ensure_private_directory(file.parent_path());
    std::ofstream output(file, std::ios::binary);
    output << bytes;
    output.close();
    check(bool(output), "fixture write");
}

std::vector<std::string> names(const std::vector<ProjectFile> &files) {
    std::vector<std::string> result;

    for (const auto &file : files) {
        result.push_back(file.name);
    }

    return result;
}
} // namespace

int main(int argc, char **argv) {
    try {
        check(argc == 3, "project root and scratch directory required");
        const auto project = std::filesystem::canonical(argv[1]);
        const auto root = std::filesystem::path(argv[2]) / ("project-files-" + random_token());
        ensure_private_directory(root);
        struct Cleanup {
            std::filesystem::path path;

            ~Cleanup() {
                std::error_code ignored;
                std::filesystem::remove_all(path, ignored);
            }
        } cleanup{root};

        // An installation without src/ reads the copies embedded in the launcher.
        check(!project_source_checkout(root), "installation without src/");
        check(project_file_json(root, "src/prepare/toolchain.json", 256 * 1024)
                      .contains("libraries"),
              "embedded preparation tool pins");
        const auto runtime = project_file_bytes(root, "src/guest/qnx/qnx-runtime.sh", 1 << 20);
        check(runtime.starts_with("#!/proc/boot/bin/ksh\n") &&
                      project_file_sha256(root, "src/guest/qnx/qnx-runtime.sh", 1 << 20) ==
                              sha256_bytes(runtime),
              "embedded guest script and its SHA-256");
        const auto qnx = names(project_directory_files(root, "src/guest/qnx", false));
        check(std::find(qnx.begin(), qnx.end(), "src/guest/qnx/qnx-runtime.sh") != qnx.end() &&
                      std::is_sorted(qnx.begin(), qnx.end()) &&
                      names(project_directory_files(root, "src", true)).size() > qnx.size(),
              "embedded listings");
        // Sources the launcher only hashed in a checkout are not embedded.
        check(failure([&] {
                  project_file_sha256(root, "src/app/core/model.cpp", 1 << 20);
              }).starts_with("PROJECT_FILE:") &&
                      project_directory_files(root, "src/prepare/java", true).empty(),
              "no own sources are embedded for their SHA-256");
        check(failure([&] {
                  project_file_sha256(root, "src/missing.txt", 1 << 20);
              }).starts_with("PROJECT_FILE:"),
              "a file outside the index is rejected");
        check(failure([&] {
                  project_file_bytes(root, "src/prepare/toolchain.json", 1);
              }).starts_with("PROJECT_FILE:"),
              "the bound applies to embedded content");
        // The javaHelper pin identifies the helper of an installation; a checkout names its
        // sources.
        const auto installed = java_helper_source_inputs(root, false),
                   checkout = java_helper_source_inputs(project, true);
        check(installed.at("asset_sha256") ==
                              embedded_toolchain_manifest().at("javaHelper").at("sha256") &&
                      !installed.contains("sources") && checkout.contains("sources") &&
                      installed.at("dependencies") == checkout.at("dependencies") &&
                      java_helper_source_inputs(project, false) == installed &&
                      !installed.contains("libraries_spec_sha256") &&
                      !installed.contains("tool_pins_sha256") &&
                      !checkout.contains("libraries_spec_sha256") &&
                      !checkout.contains("tool_pins_sha256"),
              "Java helper inputs of an installation and of its checkout");

        const auto metadata = root / "metadata-fixture";
        const auto libraries_file = metadata / "src/prepare/libraries.json",
                   tools_file = metadata / "src/prepare/toolchain.json";
        auto libraries = project_file_json(root, "src/prepare/libraries.json", 256 * 1024);
        const auto tools = project_file_json(root, "src/prepare/toolchain.json", 256 * 1024);
        put(libraries_file, libraries.dump());
        put(tools_file, tools.dump());
        check(project_source_checkout(metadata) &&
                      java_helper_source_inputs(metadata, false) == installed,
              "asset helper inputs use the same exact external dependency pins in a checkout");
        put(libraries_file, libraries.dump(2) + " \n");
        put(tools_file, tools.dump(2) + " \n");
        check(java_helper_source_inputs(metadata, false) == installed,
              "metadata whitespace is not an input pin");
        const auto library = "lib/" + libraries.at(0).at("filename").get<std::string>();
        const std::string changed_pin(
                64, libraries.at(0).at("sha256") == std::string(64, '0') ? '1' : '0');
        auto changed = installed;
        changed["dependencies"][library] = changed_pin;
        libraries[0]["sha256"] = changed_pin;
        put(libraries_file, libraries.dump());
        check(java_helper_source_inputs(metadata, false) == changed,
              "one external dependency pin changes exactly its helper input");
        libraries[0]["sha256"] = "not-a-sha256";
        put(libraries_file, libraries.dump());
        check(failure([&] {
                  java_helper_source_inputs(metadata, false);
              }).starts_with("JAVA_TOOLCHAIN:"),
              "malformed external dependency pins remain rejected");

        // A root with src/ reads its own tree.
        put(root / "src/a/b.json", R"({"x":1})");
        put(root / "src/a/c.txt", "c");
        put(root / "src/a/d/e.txt", "e");
        check(project_source_checkout(root), "source checkout");
        check(project_file_json(root, "src/a/b.json", 64).at("x") == 1 &&
                      project_file_sha256(root, "src/a/c.txt", 64) == sha256_bytes("c") &&
                      project_file_bytes(root, "src/a/d/e.txt", 64) == "e",
              "tree files");
        check(names(project_directory_files(root, "src/a", false)) ==
                              std::vector<std::string>{"src/a/b.json", "src/a/c.txt"} &&
                      names(project_directory_files(root, "src/a", true)) ==
                              std::vector<std::string>{"src/a/b.json", "src/a/c.txt",
                                                       "src/a/d/e.txt"},
              "tree listings");
        check(failure([&] {
                  project_file_sha256(root, "src/prepare/toolchain.json", 256 * 1024);
              }).starts_with("PROJECT_FILE:"),
              "a checkout never falls back to the embedded copy");
        check(failure([&] {
                  project_file_sha256(root, "src/a/c.txt", 0);
              }).starts_with("PROJECT_FILE:"),
              "the bound applies to tree files");
        std::cout << "project files of installations and checkouts passed\n";

        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';

        return 1;
    }
}
