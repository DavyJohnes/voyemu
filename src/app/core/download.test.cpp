#include "download.hpp"
#include "disk_space.hpp"
#include "interruption.hpp"
#include "model.hpp"
#include "os/https.hpp"
#include "process.hpp"
#include "progress.hpp"
#include <cassert>
#include <chrono>
#include <exception>
#include <fstream>
#include <iostream>
#include <limits>
#include <regex>
#include <sstream>

int main(int argc, char **argv) try {
    using namespace voyemu;

    if (argc == 3 && std::string_view(argv[1]) == "--https") {
        try {
            const DownloadPin certificate{
                    "https://android.googlesource.com/platform/build/+/refs/tags/android-9.0.0_r61/target/product/security/platform.x509.pem?format=TEXT",
                    "9837de028f460c35cc8d3fa45f14eecce30f6fbfe4b93d399aef1acb80c20d14", 32768,
                    DownloadEncoding::base64};
            const auto result = download_pinned(certificate, std::filesystem::path(argv[2]), false,
                                                0);
            std::cout << Json{{"native_https", true},
                              {"bytes", result.bytes},
                              {"cached", result.cached},
                              {"sha256", sha256_file(result.file)}}
                                 .dump()
                      << '\n';

            return 0;
        } catch (const std::exception &error) {
            std::cerr << error.what() << '\n';

            return 1;
        }
    }

    if (argc != 1) {
        return 2;
    }

    const auto root = std::filesystem::canonical(std::filesystem::temp_directory_path()) /
                      ("voyemu-download-" + random_token());
    ensure_private_directory(root);
    struct Cleanup {
        std::filesystem::path path;

        ~Cleanup() {
            std::filesystem::remove_all(path);
        }
    } cleanup{root};
    const DownloadPin pin{"https://example.invalid/pinned", sha256_bytes("abc"), 3};
    unsigned requests = 0;
    const DownloadTransport transport = [&](const DownloadPin &requested,
                                            const DownloadSink &sink) {
        assert(requested.sha256 == pin.sha256);
        ++requests;
        sink("a");
        sink("bc");
    };
    auto result = download_pinned(pin, root / "cache", false, 0, transport);
    assert(result.bytes == 3 && !result.cached && requests == 1 &&
           sha256_file(result.file) == pin.sha256);
    assert(download_pinned(pin, root / "cache", true, 0, transport).cached && requests == 1);
    auto rejects = [](const auto &action) {
        bool failed = false;
        try {
            action();
        } catch (const std::exception &) {
            failed = true;
        }

        assert(failed);
    };

    for (const auto &url : {"", "x", "http://example.invalid", "https://user@example.invalid/path",
                            "https:///empty", "https://example.invalid/#fragment"}) {
        auto bad = pin;
        bad.url = url;
        rejects([&] {
            download_pinned(bad, root / "invalid", false, 0, transport);
        });
        assert(!std::filesystem::exists(root / "invalid"));
    }

    rejects([&] {
        download_pinned(pin, root / "offline", true, 0, transport);
    });
    assert(requests == 1);
    rejects([&] {
        download_pinned(pin, root / "overflow", false, 0, [](const auto &, const auto &sink) {
            sink("abcd");
        });
    });
    rejects([&] {
        download_pinned(pin, root / "mismatch", false, 0, [](const auto &, const auto &sink) {
            sink("bad");
        });
    });

    for (const auto *name : {"overflow", "mismatch"}) {
        for (const auto &entry : std::filesystem::directory_iterator(root / name)) {
            assert(!entry.path().filename().string().starts_with(".part-"));
        }
    }

    // Ctrl+C during a download stops it at the next block and removes the partial file; a busy
    // server's delay is interrupted too.
    std::string interrupted;
    try {
        download_pinned(pin, root / "interrupted", false, 0, [](const auto &, const auto &sink) {
            sink("a");
            interruption::requested = true;
            sink("bc");
        });
    } catch (const std::exception &error) {
        interrupted = error.what();
    }

    assert(interrupted.starts_with("INTERRUPTED:") &&
           std::filesystem::is_empty(root / "interrupted"));
    interrupted.clear();
    const auto delay_start = std::chrono::steady_clock::now();
    try {
        download_pinned(pin, root / "interrupted-busy", false, 0, [](const auto &, const auto &) {
            throw os::HttpsBusy(429, 300);
        });
    } catch (const std::exception &error) {
        interrupted = error.what();
    }

    assert(interrupted.starts_with("INTERRUPTED:") &&
           std::chrono::steady_clock::now() - delay_start < std::chrono::seconds(5));
    interruption::requested = false;

    // A busy server is asked again after its Retry-After delay or the backoff; other failures and
    // the seventh refusal end the download.
    unsigned busy_requests = 0;
    const auto busy = download_pinned(pin, root / "busy", false, 0,
                                      [&](const auto &, const auto &sink) {
                                          ++busy_requests;

                                          if (busy_requests == 1) {
                                              throw os::HttpsBusy(429, -1);
                                          }

                                          if (busy_requests == 2) {
                                              throw os::HttpsBusy(503, 1);
                                          }

                                          sink("abc");
                                      });
    assert(busy.bytes == 3 && busy_requests == 3 && sha256_file(busy.file) == pin.sha256);
    busy_requests = 0;
    rejects([&] {
        download_pinned(pin, root / "busy-exhausted", false, 0, [&](const auto &, const auto &) {
            ++busy_requests;

            throw os::HttpsBusy(429, 0);
        });
    });
    assert(busy_requests == 7);
    busy_requests = 0;
    rejects([&] {
        download_pinned(pin, root / "missing", false, 0, [&](const auto &, const auto &) {
            ++busy_requests;

            throw std::runtime_error("DOWNLOAD_HTTP: not found");
        });
    });
    assert(busy_requests == 1);
    // A failed download names its cause for the console: no connection, a stalled transfer, an
    // HTTP status, a pin mismatch or an offline cache miss; each keeps its code.
    const auto failure = [&](const std::filesystem::path &cache, bool offline,
                             const DownloadTransport &failing) {
        try {
            download_pinned(pin, cache, offline, 0, failing);
        } catch (const cli::LocalizedError &error) {
            return error.render(cli::Language::english);
        }

        return std::string();
    };
    const auto failing = [](os::HttpsFailure::Kind kind, long status) -> DownloadTransport {
        return [kind, status](const DownloadPin &, const DownloadSink &) {
            throw os::HttpsFailure(kind, status, "fixture");
        };
    };
    assert(failure(root / "network", false, failing(os::HttpsFailure::Kind::unreachable, 0)) ==
           "DOWNLOAD_HTTP: no connection to example.invalid.\nCheck your internet connection and "
           "run the command again. Files already downloaded are kept.");
    assert(failure(root / "network", false, failing(os::HttpsFailure::Kind::stalled, 200)) ==
           "DOWNLOAD_HTTP: the download of pinned stalled: no data for 60 s.\nCheck your "
           "connection and run the command again. This download starts over; other files are "
           "kept.");
    assert(failure(root / "network", false, failing(os::HttpsFailure::Kind::status, 404)) ==
           "DOWNLOAD_HTTP: example.invalid answered HTTP 404 for pinned.\nThe file is not "
           "published or was removed. Update Voyemu; if the error persists, report it.");
    assert(failure(root / "network", false,
                   [](const auto &, const auto &sink) {
                       sink("bad");
                   }) ==
           "DOWNLOAD_SHA: the downloaded pinned does not match its pinned SHA-256.\nThe "
           "file was deleted. Run the command again; if the error persists, report it.");
    assert(failure(root / "network", true, transport) ==
           "DOWNLOAD_OFFLINE: pinned is not downloaded and --offline forbids downloads.\nRun the "
           "command without --offline while online.");
    assert(std::filesystem::is_empty(root / "network"));
    auto encoded = pin;
    encoded.encoding = DownloadEncoding::base64;
    const auto decoded = download_pinned(encoded, root / "base64", false, 0,
                                         [](const auto &, const auto &sink) {
                                             sink("YW");
                                             sink("Jj\n");
                                         });
    assert(decoded.bytes == 3 && sha256_file(decoded.file) == pin.sha256);
    unsigned invalid_index = 0;

    for (const auto *body : {"YWJ?", "YQ=A", "Y===", "YR==", "W"}) {
        const auto cache = root / ("bad-base64-" + std::to_string(invalid_index++));
        rejects([&] {
            download_pinned(encoded, cache, false, 0, [&](const auto &, const auto &sink) {
                sink(body);
            });
        });

        for (const auto &entry : std::filesystem::directory_iterator(cache)) {
            assert(!entry.path().filename().string().starts_with(".part-"));
        }
    }

    {
        std::ofstream out(result.file);
        out << "bad";
    }
    rejects([&] {
        download_pinned(pin, root / "cache", false, 0, transport);
    });
    assert(requests == 1);
    // A fresh download reports its start and its verified end, a cached file one line; a file that
    // this process has already reported is verified again without a line.
    auto lines = progress::reported();
    download_pinned(pin, root / "reported", false, 0, transport);
    assert(progress::reported() == lines + 2 && requests == 2);
    lines = progress::reported();
    download_pinned(pin, root / "reported", false, 0, transport);
    assert(progress::reported() == lines && requests == 2);
    ensure_private_directory(root / "copied");
    std::filesystem::copy_file(root / "reported" / (pin.sha256 + ".blob"),
                               root / "copied" / (pin.sha256 + ".blob"));
    download_pinned(pin, root / "copied", false, 0, transport);
    assert(progress::reported() == lines + 1);
    download_pinned(pin, root / "copied", false, 0, transport);
    assert(progress::reported() == lines + 1 && requests == 2);
    // A volume without the reserve fails before the request and names its free space and the
    // installation root that holds the cache, such as a CI temporary directory under .local.
    bool spaceless = false;
    try {
        download_pinned(pin, root / "space", false, std::numeric_limits<std::uint64_t>::max() / 4,
                        transport);
    } catch (const cli::LocalizedError &error) {
        spaceless = std::string(error.what())
                            .ends_with("on the volume of " +
                                       installation_root_of(root / "space").string() +
                                       " or move Voyemu to another disk.");
    }

    assert(spaceless && requests == 2);

    // The console lines of a fresh deps: each step that downloads is a block, an archive that an
    // earlier step downloaded is not reported again, and a cached step follows after an empty line.
    const auto serve = [](const DownloadPin &requested, const DownloadSink &sink) {
        sink(requested.url.ends_with("/platform") ? "xyz" : "abc");
    };
    DownloadPin emulator = pin,
                platform{"https://example.invalid/platform", sha256_bytes("xyz"), 3};
    emulator.name = "Android Emulator 36.3.10";
    platform.name = "Android SDK Platform 34";
    platform.label = "java_platform";
    platform.version = "34";
    std::ostringstream console;
    auto *prior = std::cerr.rdbuf(console.rdbuf());
    progress::console(cli::Language::english, false);
    {
        progress::Steps steps;
        progress::block();
        steps.begin(false);
        download_pinned(emulator, root / "deps", false, 0, serve);
        progress::report("extract", download_names(emulator));
        steps.end("sdk");
        steps.begin();
        steps.end("qnx_host");
        steps.begin();
        download_pinned(emulator, root / "deps", false, 0, serve);
        download_pinned(platform, root / "deps", false, 0, serve);
        steps.end("java_tools");
        steps.begin();
        steps.end("guest_java");
        progress::separate();
    }
    const auto fresh = console.str();
    // A repeated deps is a compact list; this process already showed lines, so it starts with an
    // empty line.
    console.str({});
    {
        progress::Steps steps;
        progress::block();
        progress::report("sdk_verify");
        steps.begin(false);
        steps.end("sdk");

        for (const auto *name : {"qnx_host", "java_runtime", "java_tools", "guest_java"}) {
            steps.begin();
            steps.end(name);
        }

        progress::separate();
    }
    std::cerr.rdbuf(prior);
    const std::string rate = R"(\(\d+(\.\d)? (B|KiB)/s\), SHA-256 verified\n)";
    assert(std::regex_match(
            fresh,
            std::regex("Downloading Android Emulator 36\\.3\\.10 from example\\.invalid\n"
                       "Android Emulator 36\\.3\\.10: downloaded 3 B in 0 s " +
                       rate +
                       "Extracting Android Emulator 36\\.3\\.10\n"
                       "Android SDK tools: installed\n\n"
                       "QNX engine: already installed\n\n"
                       "Downloading Android platform 34 for the Java tools from example\\.invalid\n"
                       "Android platform 34 for the Java tools: downloaded 3 B in 0 s " +
                       rate +
                       "Java tools of preparation: installed\n\n"
                       "Guest Java code: already installed\n\n")));
    assert(console.str() == "\nChecking the installed Android SDK tools\n"
                            "Android SDK tools: already installed\n"
                            "QNX engine: already installed\n"
                            "Java runtime: already installed\n"
                            "Java tools of preparation: already installed\n"
                            "Guest Java code: already installed\n\n");
    std::cout << "pinned native download/cache; bounds, pin failure and owned partial cleanup\n";
} catch (const std::exception &error) {
    std::cerr << error.what() << '\n';

    return 1;
}
