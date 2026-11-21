#include "android_java.hpp"
#include "error.hpp"
#include "logging.hpp"
#include "process.hpp"
#include "progress.hpp"
namespace voyemu {
std::filesystem::path install_android_java(const std::filesystem::path &root, const Json &pin,
                                           bool offline, std::uint64_t reserve,
                                           const DownloadTransport &transport) {
    const auto sha = pin.at("sha256").get<std::string>();
    const auto folder = root / ".local/toolchains/android-java" / sha;
    const auto archive = folder / "android-java.zip";

    if (std::filesystem::exists(std::filesystem::symlink_status(archive))) {
        if (!std::filesystem::is_regular_file(std::filesystem::symlink_status(archive)) ||
            sha256_file(archive) != sha) {
            throw VoyemuError("ANDROID_JAVA",
                              "installed guest Java differs from its pin: " + archive.string());
        }

        return archive;
    }

    std::filesystem::path blob;

    try {
        blob = download_pinned({pin.at("url").get<std::string>(),
                                sha,
                                256ULL << 20,
                                DownloadEncoding::raw,
                                {},
                                "guest_java"},
                               root / ".local/toolchains/downloads/android-java", offline, reserve,
                               transport)
                       .file;
    } catch (const std::runtime_error &error) {
        const std::string message = error.what();

        // Only an asset that is not published or not cached offline falls back; a network failure
        // stays an error.
        if (!download_unpublished(std::current_exception()) &&
            !message.starts_with("DOWNLOAD_OFFLINE:")) {
            throw;
        }

        logging::write(logging::Level::debug, "prepare", "guest Java download failed",
                       {{"sha256", sha}, {"download", message}});
        progress::report("guest_java_build");

        return {};
    }

    ensure_private_directory(folder);
    const auto temporary = folder / (".part-" + random_token());
    std::filesystem::copy_file(blob, temporary);

    if (sha256_file(temporary) != sha) {
        std::filesystem::remove(temporary);

        throw VoyemuError("ANDROID_JAVA", "copied guest Java differs from its pin");
    }

    std::filesystem::rename(temporary, archive);

    return archive;
}
} // namespace voyemu
