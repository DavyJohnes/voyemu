#include "progress.hpp"
#include "disk_space.hpp"
#include "logging.hpp"
#include "process.hpp"
#include <cassert>
#include <exception>
#include <iostream>
#include <limits>
#include <sstream>
#include <tuple>
#include <vector>

namespace {
using namespace voyemu;
using Line = std::tuple<std::string, progress::Arguments, bool>;

// A meter on a fake clock that collects its lines.
struct Recorder {
    std::chrono::steady_clock::time_point now{};
    std::vector<Line> lines;

    progress::Meter meter(bool terminal, const std::string &kind = "download") {
        return {kind,
                {{"name", "Android Emulator 36.3.10"}, {"host", "dl.google.com"}},
                terminal,
                [this](std::string_view key, const progress::Arguments &arguments, bool live) {
                    lines.emplace_back(std::string(key), arguments, live);
                },
                [this] {
                    return now;
                }};
    }
};
} // namespace

int main() try {
    const auto english = cli::Language::english, russian = cli::Language::russian;

    assert(progress::size_text(900, english) == "900 B");
    assert(progress::size_text(1536, english) == "1.5 KiB");
    assert(progress::size_text(1536, russian) == "1,5 КиБ");
    assert(progress::size_text(320969728, russian) == "306,1 МиБ");
    assert(progress::size_text(390093951, english) == "372 MiB");
    assert(progress::size_text(2200000000ULL, english) == "2 GiB");
    assert(progress::rate_text(1ULL << 20, russian) == "1 МиБ/с");
    assert(progress::rate_text(1536, english) == "1.5 KiB/s");
    assert(progress::duration_text(42, english) == "42 s");
    assert(progress::duration_text(42, russian) == "42 с");
    assert(progress::duration_text(120, english) == "2 min");
    assert(progress::duration_text(65, russian) == "1 мин 5 с");
    assert(progress::duration_text(3723, english) == "1 h 2 min");
    assert(progress::url_host("https://github.com/voyemu/voyemu-deps/releases/x.zip") ==
           "github.com");
    assert(progress::url_host("https://127.0.0.1:8443/ok") == "127.0.0.1");
    assert(progress::url_file("https://github.com/a/jdk-25.0.5%2B8/OpenJDK25U.tar.gz?x=1") ==
           "OpenJDK25U.tar.gz");
    assert(progress::url_file("https://example.invalid/v/jdk%2B8") == "jdk+8");

    // Log output: a status line at each further 10% of a known size, then the final line.
    {
        Recorder recorder;
        {
            auto meter = recorder.meter(false);
            meter.length(100ULL << 20);

            for (int second = 1; second <= 100; ++second) {
                recorder.now += std::chrono::seconds(1);
                meter.advance(1ULL << 20);
            }

            meter.finish();
        }
        assert(recorder.lines.size() == 11);
        const auto &[start, start_arguments, start_live] = recorder.lines.front();
        assert(start == "download_size" && !start_live &&
               start_arguments.at("total") == "100 MiB" &&
               start_arguments.at("name") == "Android Emulator 36.3.10" &&
               start_arguments.at("host") == "dl.google.com");

        for (std::size_t step = 1; step <= 9; ++step) {
            const auto &[key, arguments, live] = recorder.lines[step];
            assert(key == "download_status_size" && !live &&
                   arguments.at("percent") == std::to_string(step * 10) &&
                   arguments.at("rate") == "1 MiB/s");
        }

        const auto &first = std::get<1>(recorder.lines[1]);
        assert(first.at("done") == "10 MiB" && first.at("eta") == "1 min 30 s");
        const auto &[done, done_arguments, done_live] = recorder.lines.back();
        assert(done == "download_done" && !done_live && done_arguments.at("done") == "100 MiB" &&
               done_arguments.at("time") == "1 min 40 s" && done_arguments.at("rate") == "1 MiB/s");
    }

    // Log output of a fast transfer: only the start and the final line.
    {
        Recorder recorder;
        auto meter = recorder.meter(false);
        meter.length(10ULL << 20);

        for (int tick = 1; tick <= 10; ++tick) {
            recorder.now += std::chrono::milliseconds(150);
            meter.advance(1ULL << 20);
        }

        meter.finish();
        assert(recorder.lines.size() == 2 && std::get<0>(recorder.lines.back()) == "download_done");
    }

    // Log output of a slow or unsized transfer: a status line after 30 s without one.
    for (const std::uint64_t total : {0ULL, 1ULL << 30}) {
        Recorder recorder;
        auto meter = recorder.meter(false);

        if (total) {
            meter.length(total);
        }

        for (int tick = 1; tick <= 7; ++tick) {
            recorder.now += std::chrono::seconds(10);
            meter.advance(1024);
        }

        assert(recorder.lines.size() == 3);
        assert(std::get<0>(recorder.lines[1]) ==
               (total ? "download_status_size" : "download_status"));
        assert(std::get<0>(recorder.lines.front()) == (total ? "download_size" : "download"));
        // Without a size the meter starts with the first bytes, 10 s later.
        assert(std::get<1>(recorder.lines[2]).at("done") == (total ? "6 KiB" : "7 KiB"));
    }

    // Terminal: a live status line at most twice a second; an unfinished live line is ended.
    {
        Recorder recorder;
        {
            auto meter = recorder.meter(true);
            meter.length(1ULL << 20);

            for (int tick = 1; tick <= 20; ++tick) {
                recorder.now += std::chrono::milliseconds(100);
                meter.advance(1024);
            }
        }
        assert(recorder.lines.size() == 6);

        for (std::size_t line = 1; line <= 4; ++line) {
            assert(std::get<0>(recorder.lines[line]) == "download_status_size" &&
                   std::get<2>(recorder.lines[line]));
        }

        assert(std::get<1>(recorder.lines[4]).at("percent") == "1" &&
               std::get<1>(recorder.lines[4]).at("time") == "2 s");
        assert(std::get<0>(recorder.lines.back()).empty() && !std::get<2>(recorder.lines.back()));
    }

    // The kind names the lines of other long operations.
    {
        Recorder recorder;
        auto meter = recorder.meter(false, "extract");
        meter.finish();
        assert(std::get<0>(recorder.lines.front()) == "extract" &&
               std::get<0>(recorder.lines.back()) == "extract_done");
    }

    // The console renders catalog messages in the selected language and counts the lines.
    std::ostringstream captured;
    auto *prior = std::cerr.rdbuf(captured.rdbuf());
    const auto before = progress::reported();
    progress::console(cli::Language::russian, false);
    progress::report("extract", {{"name", "CMake 3.22.1"}});
    progress::report("sdk_cached");
    std::cerr.rdbuf(prior);
    assert(progress::reported() == before + 2);
    assert(captured.str() == "Распаковка CMake 3.22.1\nИнструменты Android SDK: уже установлены\n");

    // Blocks start after an empty line; a label names a catalog component in its case.
    captured.str({});
    prior = std::cerr.rdbuf(captured.rdbuf());
    progress::block();
    progress::report("extract", {{"label", "qnx_host"}, {"version", "11.1.2"}});
    progress::report("qnx_host_installed");
    progress::report("download_done", {{"label", "java_runtime"},
                                       {"version", "25.0.5+8"},
                                       {"done", "18,4 МиБ"},
                                       {"time", "1 с"},
                                       {"rate", "18,4 МиБ/с"}});
    progress::separate();
    std::cerr.rdbuf(prior);
    assert(captured.str() == "\nРаспаковка движка QNX 11.1.2\nДвижок QNX: установлен\n"
                             "Среда выполнения Java 25.0.5+8: загружено 18,4 МиБ за 1 с "
                             "(18,4 МиБ/с), SHA-256 совпадает\n\n");

    // A short disk space error in whole GiB: free rounded down, required up, their difference to
    // free; the path is the installation root.
    constexpr std::uint64_t gib = 1ULL << 30;
    const auto root = std::filesystem::path("/Users/x/voyemu");
    assert(installation_root_of(root / ".local/toolchains/downloads/native") == root);
    assert(installation_root_of(root / "machines/free") == root);
    assert(installation_root_of("/data/other") == "/data/other");
    const auto edge = disk_space_error("DOWNLOAD_SPACE", root, 12 * gib + gib * 99 / 100, 20 * gib);
    assert(std::string(edge.what()) ==
           "DOWNLOAD_SPACE: not enough disk space: 12 GiB free, 20 GiB required.\nFree up 8 GiB "
           "on the volume of /Users/x/voyemu or move Voyemu to another disk.");
    assert(edge.render(russian) ==
           "DOWNLOAD_SPACE: мало места на диске: свободно 12 ГиБ, нужно 20 ГиБ.\nОсвободите 8 ГиБ "
           "на томе с /Users/x/voyemu или перенесите Voyemu на другой диск.");
    const auto small = disk_space_error("ZIP_SPACE", root, gib + gib / 2, 2 * gib + gib / 100);
    assert(std::string(small.what())
                   .starts_with(
                           "ZIP_SPACE: not enough disk space: 1 GiB free, 3 GiB required.\nFree up 2 GiB "));

    // A real volume without the margin fails with that error and passes with none.
    const auto temporary = std::filesystem::canonical(std::filesystem::temp_directory_path());
    require_disk_space("DOWNLOAD_SPACE", temporary / "absent" / "cache", 0, 0);
    bool refused = false;
    try {
        require_disk_space("DOWNLOAD_SPACE", temporary / "absent" / "cache", gib,
                           std::numeric_limits<std::uint64_t>::max() / 4);
    } catch (const cli::LocalizedError &failure) {
        refused = std::string_view(failure.what())
                          .starts_with("DOWNLOAD_SPACE: not enough disk space: ");
    }

    assert(refused);
    std::cout << "progress lines, byte meter throttling and disk space messages passed\n";
} catch (const std::exception &error) {
    std::cerr << error.what() << '\n';

    return 1;
}
