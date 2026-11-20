#include "qnx_update.hpp"
#include "check.hpp"
#include "error.hpp"
#include "interruption.hpp"
#include "ordinary_path.hpp"
#include "os/digest.hpp"
#include "os/fs.hpp"
#include "process.hpp"
#include <algorithm>
#include <array>
#include <fstream>
#include <stdexcept>
#include <vector>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <winioctl.h>
#else
#include <dlfcn.h>
#include <sys/mman.h>
#endif
namespace voyemu {
namespace {
constexpr CodeCheck need{"invalid OEM update"};

void ordinary(const std::filesystem::path &path) {
    ordinary_path(path, need, PathForm::Normalized, "QNX_UPDATE_PATH", "QNX_UPDATE_LINK");
}

struct Library {
#ifdef _WIN32
    HMODULE handle{};

    explicit Library(const std::filesystem::path &file)
        : handle(LoadLibraryExW(file.c_str(), nullptr,
                                LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR |
                                        LOAD_LIBRARY_SEARCH_DEFAULT_DIRS)) {}

    ~Library() {
        if (handle) {
            FreeLibrary(handle);
        }
    }

    void *get(const char *name) {
        return reinterpret_cast<void *>(GetProcAddress(handle, name));
    }
#else
    void *handle{};

    explicit Library(const std::filesystem::path &file)
        : handle(dlopen(file.c_str(), RTLD_NOW | RTLD_LOCAL)) {}

    ~Library() {
        if (handle) {
            dlclose(handle);
        }
    }

    void *get(const char *name) {
        return dlsym(handle, name);
    }
#endif

    template <class Function> Function symbol(const char *name) {
        auto value = reinterpret_cast<Function>(get(name));
        need(value != nullptr, "QNX_UPDATE_CODEC_SYMBOL");

        return value;
    }
};
struct ZeroReference {
    void *data{};
    std::size_t bytes{};
#ifdef _WIN32
    HANDLE file = INVALID_HANDLE_VALUE, mapping{};
#endif

    explicit ZeroReference(std::size_t count, const std::filesystem::path &directory)
        : bytes(count) {
#ifdef _WIN32
        try {
            const auto name = directory / ("qnx-zero-reference-" + random_token() + ".bin");
            file = CreateFileW(name.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                               FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, nullptr);
            need(file != INVALID_HANDLE_VALUE, "QNX_UPDATE_REFERENCE_FILE");
            DWORD returned = 0;
            need(DeviceIoControl(file, FSCTL_SET_SPARSE, nullptr, 0, nullptr, 0, &returned,
                                 nullptr) != 0,
                 "QNX_UPDATE_REFERENCE_SPARSE");
            LARGE_INTEGER end{};
            end.QuadPart = static_cast<LONGLONG>(bytes);
            need(SetFilePointerEx(file, end, nullptr, FILE_BEGIN) && SetEndOfFile(file),
                 "QNX_UPDATE_REFERENCE_EXTENT");
            mapping = CreateFileMappingW(file, nullptr, PAGE_READONLY, 0, 0, nullptr);
            need(mapping != nullptr, "QNX_UPDATE_REFERENCE_MAPPING");
            data = MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, bytes);
            need(data != nullptr, "QNX_UPDATE_REFERENCE_MAP");
        } catch (...) {
            if (data) {
                UnmapViewOfFile(data);
            }

            if (mapping) {
                CloseHandle(mapping);
            }

            if (file != INVALID_HANDLE_VALUE) {
                CloseHandle(file);
            }

            throw;
        }
#else
        (void)directory;
        data = mmap(nullptr, bytes, PROT_READ, MAP_PRIVATE | MAP_ANON, -1, 0);

        if (data == MAP_FAILED) {
            data = nullptr;
        }
#endif
        need(data != nullptr, "QNX_UPDATE_REFERENCE_MAP");
    }

    ~ZeroReference() {
#ifdef _WIN32
        if (data) {
            UnmapViewOfFile(data);
        }

        if (mapping) {
            CloseHandle(mapping);
        }

        if (file != INVALID_HANDLE_VALUE) {
            CloseHandle(file);
        }
#else
        if (data) {
            munmap(data, bytes);
        }
#endif
    }
};
struct InBuffer {
    const void *src;
    std::size_t size, pos;
};
struct OutBuffer {
    void *dst;
    std::size_t size, pos;
};
} // namespace

static void decode_patch(const std::filesystem::path &patch, std::string_view patch_sha256,
                         std::uint64_t image_bytes, std::string_view image_sha256,
                         const std::filesystem::path &library, std::string_view library_sha256,
                         const std::filesystem::path &output) {
    ordinary(patch);
    ordinary(library);
    ordinary(output);
    need(sha256_text(patch_sha256) && (image_sha256.empty() || sha256_text(image_sha256)) &&
                 sha256_text(library_sha256),
         "QNX_UPDATE_SHA_FORMAT");
    need(image_bytes > 0 && image_bytes <= (4ULL << 30), "QNX_UPDATE_IMAGE_BOUNDS");
    need(std::filesystem::is_regular_file(patch) && std::filesystem::hard_link_count(patch) == 1 &&
                 std::filesystem::file_size(patch) > 0 &&
                 std::filesystem::file_size(patch) <= (4ULL << 30) &&
                 sha256_file(patch) == patch_sha256,
         "QNX_UPDATE_PATCH_SHA");
    need(std::filesystem::is_regular_file(library) &&
                 std::filesystem::file_size(library) <= (64ULL << 20) &&
                 sha256_file(library) == library_sha256,
         "QNX_UPDATE_LIBRARY_SHA");
    need(!std::filesystem::exists(output) && std::filesystem::is_directory(output.parent_path()),
         "QNX_UPDATE_FRESH_OUTPUT");
    const auto patch_size = std::filesystem::file_size(patch);
    Library codec(library);
    need(codec.handle != nullptr, "QNX_UPDATE_CODEC_LOAD");
    const auto create = codec.symbol<void *(*)()>("ZSTD_createDCtx");
    const auto release = codec.symbol<std::size_t (*)(void *)>("ZSTD_freeDCtx");
    const auto prefix =
            codec.symbol<std::size_t (*)(void *, const void *, std::size_t)>("ZSTD_DCtx_refPrefix");
    const auto parameter =
            codec.symbol<std::size_t (*)(void *, int, int)>("ZSTD_DCtx_setParameter");
    const auto decode =
            codec.symbol<std::size_t (*)(void *, OutBuffer *, InBuffer *)>("ZSTD_decompressStream");
    const auto error = codec.symbol<unsigned (*)(std::size_t)>("ZSTD_isError");
    void *context = create();
    need(context != nullptr, "QNX_UPDATE_CODEC_CONTEXT");
    struct Context {
        void *value;
        std::size_t (*release)(void *);

        ~Context() {
            release(value);
        }
    } context_guard{context, release};
    ZeroReference zero(static_cast<std::size_t>(image_bytes), output.parent_path());
    need(!error(parameter(context, 100, 31)) && !error(prefix(context, zero.data, zero.bytes)),
         "QNX_UPDATE_CODEC_REFERENCE");
    std::ifstream input(patch, std::ios::binary);
    std::ofstream out(output, std::ios::binary);
    need(bool(input) && bool(out), "QNX_UPDATE_OPEN");
    bool complete = false;
    struct Output {
        const std::filesystem::path &path;
        std::ofstream &stream;
        bool &complete;

        ~Output() {
            if (!complete) {
                try {
                    stream.close();
                } catch (...) { /* Ignored: destructors must not throw. */
                }

                std::error_code ignored;
                std::filesystem::remove(path, ignored);
            }
        }
    } output_guard{output, out, complete};
    std::array<char, 65536> packed{}, plain{};
    std::uint64_t extent = 0, consumed = 0;
    bool ended = false;

    while (input) {
        interruption::check();
        input.read(packed.data(), packed.size());
        const auto count = static_cast<std::size_t>(input.gcount());

        if (!count) {
            break;
        }

        need(!ended, "QNX_UPDATE_TRAILING_STREAM");
        InBuffer in{packed.data(), count, 0};
        bool flush = false;

        while (in.pos < in.size || flush) {
            const auto before = in.pos;
            OutBuffer decoded{plain.data(), plain.size(), 0};
            const auto remaining = decode(context, &decoded, &in);
            need(!error(remaining), "QNX_UPDATE_DECODE");
            need(decoded.pos <= image_bytes - extent, "QNX_UPDATE_OUTPUT_BOUNDS");

            if (decoded.pos) {
                if (std::any_of(plain.begin(),
                                plain.begin() + static_cast<std::ptrdiff_t>(decoded.pos),
                                [](char value) {
                                    return value != 0;
                                })) {
                    out.seekp(static_cast<std::streamoff>(extent));
                    out.write(plain.data(), static_cast<std::streamsize>(decoded.pos));
                    need(bool(out), "QNX_UPDATE_WRITE");
                }

                extent += decoded.pos;
            }

            if (in.pos == before && decoded.pos == 0 && in.pos == in.size) {
                break;
            }

            need(in.pos > before || decoded.pos > 0, "QNX_UPDATE_NO_PROGRESS");

            if (remaining == 0) {
                need(in.pos == in.size, "QNX_UPDATE_TRAILING_STREAM");
                ended = true;
                break;
            }

            flush = decoded.pos == plain.size();
        }

        consumed += count;
    }

    need(input.eof() && ended && consumed == patch_size && extent == image_bytes,
         "QNX_UPDATE_TRUNCATED");
    out.close();
    need(bool(out), "QNX_UPDATE_CLOSE");
    os::resize_file(output, image_bytes);
    need(image_sha256.empty() || sha256_file(output) == image_sha256, "QNX_UPDATE_IMAGE_SHA");
    need(std::filesystem::file_size(patch) == patch_size && sha256_file(patch) == patch_sha256 &&
                 sha256_file(library) == library_sha256,
         "QNX_UPDATE_INPUT_CHANGED");
    complete = true;
}

namespace {
std::string md5(const std::filesystem::path &path) {
    os::Md5 hash;
    std::ifstream input(path, std::ios::binary);

    need(bool(input), "QNX_UPDATE_MD5_OPEN");
    std::array<char, 65536> buffer{};

    while (input) {
        interruption::check();
        input.read(buffer.data(), buffer.size());
        const auto count = input.gcount();

        if (count > 0) {
            hash.update(buffer.data(), std::size_t(count));
        }
    }

    need(input.eof(), "QNX_UPDATE_MD5_READ");

    return os::hex_digest(hash.finish());
}

std::string collect(const std::vector<QnxUpdateSegment> &segments, std::uint64_t image_bytes,
                    std::string_view image_sha256, std::string_view image_md5,
                    const std::filesystem::path &library, std::string_view library_sha256,
                    const std::filesystem::path &output,
                    const std::function<void(std::uint64_t)> &advance = {}) {
    ordinary(output);
    need(!std::filesystem::exists(output) && std::filesystem::is_directory(output.parent_path()),
         "QNX_UPDATE_FRESH_OUTPUT");
    need(!segments.empty() && segments.size() <= 64 && image_bytes > 0 &&
                 image_bytes <= (4ULL << 30),
         "QNX_UPDATE_SEGMENT_COUNT");
    need((sha256_text(image_sha256) && image_md5.empty()) ||
                 (image_sha256.empty() && hex_text(image_md5, 32)),
         "QNX_UPDATE_IMAGE_PIN");
    std::uint64_t total = 0;

    for (std::size_t index = 0; index < segments.size(); ++index) {
        const auto &segment = segments[index];
        need(segment.index == index && segment.bytes > 0 && segment.bytes <= image_bytes - total &&
                     sha256_text(segment.sha256),
             "QNX_UPDATE_SEGMENT_ORDER");
        total += segment.bytes;
    }

    need(total == image_bytes, "QNX_UPDATE_SEGMENT_EXTENT");
    const auto stage = output.parent_path() / ("qnx-segments-" + random_token());
    need(std::filesystem::create_directory(stage), "QNX_UPDATE_SEGMENT_STAGE");
    bool complete = false;
    struct Cleanup {
        std::filesystem::path stage, output;
        bool &complete;

        ~Cleanup() {
            std::error_code ignored;
            std::filesystem::remove_all(stage, ignored);

            if (!complete) {
                std::filesystem::remove(output, ignored);
            }
        }
    } cleanup{stage, output, complete};
    std::ofstream target(output, std::ios::binary);
    need(bool(target), "QNX_UPDATE_SEGMENT_OUTPUT");
    std::uint64_t offset = 0;

    for (const auto &segment : segments) {
        const auto part = stage / "part.img";
        decode_patch(segment.patch, segment.sha256, segment.bytes, {}, library, library_sha256,
                     part);
        std::ifstream source(part, std::ios::binary);
        std::array<char, 65536> buffer{};
        std::uint64_t copied = 0;

        while (source) {
            interruption::check();
            source.read(buffer.data(), buffer.size());
            const auto count = source.gcount();

            if (count > 0) {
                need(static_cast<std::uint64_t>(count) <= segment.bytes - copied,
                     "QNX_UPDATE_SEGMENT_COPY");

                if (std::any_of(buffer.begin(), buffer.begin() + count, [](char byte) {
                        return byte != 0;
                    })) {
                    target.seekp(static_cast<std::streamoff>(offset + copied));
                    target.write(buffer.data(), count);
                    need(bool(target), "QNX_UPDATE_SEGMENT_WRITE");
                }

                copied += count;

                if (advance) {
                    advance(static_cast<std::uint64_t>(count));
                }
            }
        }

        need(source.eof() && copied == segment.bytes, "QNX_UPDATE_SEGMENT_COPY");
        source.close();
        std::filesystem::remove(part);
        offset += copied;
    }

    target.close();
    need(bool(target), "QNX_UPDATE_SEGMENT_CLOSE");
    os::resize_file(output, image_bytes);
    const auto result = sha256_file(output);
    need(image_sha256.empty() || result == image_sha256, "QNX_UPDATE_IMAGE_SHA");
    need(image_md5.empty() || md5(output) == image_md5, "QNX_UPDATE_IMAGE_MD5");

    for (const auto &segment : segments) {
        need(sha256_file(segment.patch) == segment.sha256, "QNX_UPDATE_INPUT_CHANGED");
    }

    complete = true;

    return result;
}
} // namespace

void restore_qnx_zero_base_patch(const std::filesystem::path &patch, std::string_view patch_sha256,
                                 std::uint64_t bytes, std::string_view image_sha256,
                                 const std::filesystem::path &library,
                                 std::string_view library_sha256,
                                 const std::filesystem::path &output) {
    need(sha256_text(image_sha256), "QNX_UPDATE_SHA_FORMAT");
    decode_patch(patch, patch_sha256, bytes, image_sha256, library, library_sha256, output);
}

std::string acquire_qnx_zero_base_patch(const std::filesystem::path &patch,
                                        std::string_view patch_sha256, std::uint64_t bytes,
                                        std::string_view image_md5,
                                        const std::filesystem::path &library,
                                        std::string_view library_sha256,
                                        const std::filesystem::path &output) {
    return collect({{patch, std::string(patch_sha256), bytes, 0}}, bytes, {}, image_md5, library,
                   library_sha256, output);
}

void restore_qnx_zero_base_segments(const std::vector<QnxUpdateSegment> &segments,
                                    std::uint64_t bytes, std::string_view image_sha256,
                                    const std::filesystem::path &library,
                                    std::string_view library_sha256,
                                    const std::filesystem::path &output,
                                    const std::function<void(std::uint64_t)> &advance) {
    collect(segments, bytes, image_sha256, {}, library, library_sha256, output, advance);
}

std::string acquire_qnx_zero_base_segments(const std::vector<QnxUpdateSegment> &segments,
                                           std::uint64_t bytes, std::string_view image_md5,
                                           const std::filesystem::path &library,
                                           std::string_view library_sha256,
                                           const std::filesystem::path &output) {
    return collect(segments, bytes, {}, image_md5, library, library_sha256, output);
}
} // namespace voyemu
