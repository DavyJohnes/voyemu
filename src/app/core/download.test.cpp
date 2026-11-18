#include "download.hpp"
#include "model.hpp"
#include "os/https.hpp"
#include "process.hpp"
#include <cassert>
#include <exception>
#include <fstream>
#include <iostream>

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
    std::cout << "pinned native download/cache; bounds, pin failure and owned partial cleanup\n";
} catch (const std::exception &error) {
    std::cerr << error.what() << '\n';

    return 1;
}
