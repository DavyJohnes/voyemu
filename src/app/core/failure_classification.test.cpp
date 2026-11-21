#include "download.hpp"
#include "human_output.hpp"
#include "model.hpp"
#include "os/https.hpp"
#include "process.hpp"
#include <cassert>
#include <exception>
#include <functional>
#include <iostream>
#include <system_error>
#include <thread>
#include <vector>

namespace {
// What call throws, as the catch handler of this source sees it; null when call returns. The
// exceptions come from other sources, all compiled without RTTI, and from the C++ library.
std::exception_ptr thrown(const std::function<void()> &call) {
    try {
        call();
    } catch (const std::exception &) {
        return std::current_exception();
    }

    return {};
}
} // namespace

int main() try {
    using namespace voyemu;
    const auto root = std::filesystem::canonical(std::filesystem::temp_directory_path()) /
                      ("voyemu-failure-classification-" + random_token());
    ensure_private_directory(root);
    struct Cleanup {
        std::filesystem::path path;

        ~Cleanup() {
            std::error_code ignored;
            std::filesystem::remove_all(path, ignored);
        }
    } cleanup{root};
    const auto english = cli::Language::english;
    const DownloadPin pin{"https://example.invalid/pinned", sha256_bytes("abc"), 3};
    const auto answering = [](long status) -> DownloadTransport {
        return [status](const DownloadPin &, const DownloadSink &) {
            throw os::HttpsFailure(os::HttpsFailure::Kind::status, status, "fixture");
        };
    };
    // A VoyemuError keeps its CODE and its English message.
    const auto relative = thrown([&] {
        download_pinned(pin, "cache", false, 0, answering(404));
    });
    assert(relative && failure_code(relative) == "DOWNLOAD_PATH" &&
           !download_unpublished(relative));
    assert(human_cli_error(relative, english) ==
           "Voyemu: DOWNLOAD_PATH: absolute normalized cache required\n");
    // A failed download keeps its HTTP status, and only HTTP 404 means an unpublished asset; its
    // catalog text renders in the language.
    const auto unpublished = thrown([&] {
        download_pinned(pin, root / "unpublished", false, 0, answering(404));
    });
    assert(unpublished && failure_code(unpublished) == "DOWNLOAD_HTTP" &&
           download_unpublished(unpublished));
    assert(human_cli_error(unpublished, english) ==
           "Voyemu: DOWNLOAD_HTTP: example.invalid answered HTTP 404 for pinned.\nThe file is not "
           "published or was removed. Update Voyemu; if the error persists, report it.\n");
    const auto refused = thrown([&] {
        download_pinned(pin, root / "refused", false, 0, answering(500));
    });
    assert(refused && failure_code(refused) == "DOWNLOAD_HTTP" && !download_unpublished(refused));
    // A filesystem exception of the C++ library names its operation and path.
    const auto absent = root / "absent";
    const auto filesystem = thrown([&] {
        static_cast<void>(std::filesystem::file_size(absent));
    });
    assert(filesystem && failure_code(filesystem) == "FILESYSTEM" &&
           !download_unpublished(filesystem));
    const auto filesystem_text = human_cli_error(filesystem, english);
    assert(filesystem_text.starts_with("Voyemu: FILESYSTEM: a file operation failed: file_size " +
                                       absent.string() + ": "));
    // A JSON exception drops the library prefix of its message.
    const auto json = thrown([&] {
        write_private_json_bytes(root / "state.json", "{");
    });
    assert(json && failure_code(json) == "JSON" && !std::filesystem::exists(root / "state.json"));
    const auto json_text = human_cli_error(json, english);
    assert(json_text.starts_with("Voyemu: JSON: ") &&
           json_text.find("[json.exception.") == std::string::npos);
    // A system exception that is not a filesystem one shows its reason.
    const auto system = thrown([] {
        std::thread().join();
    });
    assert(system && failure_code(system) == "SYSTEM" &&
           human_cli_error(system, english).starts_with("Voyemu: SYSTEM: "));
    // Another exception without a CODE is an internal error.
    const auto internal = thrown([] {
        std::vector<int> values;

        values.reserve(values.max_size() + 1);
    });
    assert(internal && failure_code(internal).empty() &&
           human_cli_error(internal, english).starts_with("Voyemu: INTERNAL_ERROR: "));
    std::cout << "Thrown failures keep their CODE or kind without RTTI\n";
} catch (const std::exception &error) {
    std::cerr << error.what() << '\n';

    return 1;
}
