#include "qnx_process_ram.hpp"
#include "error.hpp"
#include <array>
#include <stdexcept>
#include <string>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "private_state_security.hpp"
#include <windows.h>
#include <winioctl.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif
namespace voyemu {
namespace {
// MTTCG stalls the OEM SMP bootstrap before timer setup; single-thread TCG reaches periodic PPI27
// delivery.
constexpr std::array<std::string_view, 2> tcg_clock{"-icount", "shift=0,align=off,sleep=on"};
} // namespace

QnxRamLaunch qnx_ram_launch(QnxEngine engine, unsigned memory_mib) {
    if (memory_mib != 8192 && memory_mib != 11713) {
        throw VoyemuError("QNX_RAMBLOCK", "unsupported guest memory extent");
    }

    QnxRamLaunch launch{
            "virt-11.0,gic-version=3,virtualization=on,secure=off,memory-backend=qnxmem",
            "file",
            {"-object", "memory-backend-file,id=qnxmem,size=" + std::to_string(memory_mib) +
                                "M,mem-path=${SESSION}/qnx-ram.bin,share=on"},
            "hvf,kernel-irqchip=on"};

    if (engine == QnxEngine::tcg_file) {
        launch.options.insert(launch.options.end(), tcg_clock.begin(), tcg_clock.end());
        launch.accelerator = "tcg,thread=single";
    }

    return launch;
}

QnxRamLaunch qnx_persistent_ram_launch(QnxEngine engine, std::string_view system,
                                       std::string_view userdata,
                                       bool windows_file_backend_verified) {
    if (os::host_windows && engine != QnxEngine::hvf && !windows_file_backend_verified) {
        throw VoyemuError("QNX_PERSISTENCE_UNSUPPORTED",
                          "verified Windows file-backed QEMU engine required");
    }

    for (const auto path : {system, userdata}) {
        if (path.empty() || path.find('\0') != std::string_view::npos ||
            path.find_first_of(",\n\r") != std::string_view::npos) {
            throw VoyemuError("QNX_PERSISTENCE_PATH", "nonempty QEMU-safe disk path required");
        }
    }

    auto launch = qnx_ram_launch(engine);
    const auto memory = launch.machine.find(",memory-backend=");

    if (memory != std::string::npos) {
        launch.machine.erase(memory);
    }

    launch.backend = "file";
    launch.options.clear();
    const std::vector<std::string> backends{
            "id=low,size=1128M,mem-path=${SESSION}/qnx-ram.bin,offset=0",
            "id=user,size=32M,mem-path=" + std::string(userdata),
            "id=mid,size=888M,mem-path=${SESSION}/qnx-ram.bin,offset=1216348160",
            "id=disk,size=1025M,mem-path=" + std::string(system),
            "id=high,size=5119M,mem-path=${SESSION}/qnx-ram.bin,offset=3222274048"};

    for (const auto &backend : backends) {
        launch.options.insert(launch.options.end(),
                              {"-object", "memory-backend-file," + backend + ",share=on"});
    }

    const std::vector<std::string> nodes{"node,nodeid=0,cpus=0-7,memdev=low",
                                         "node,nodeid=1,memdev=user", "node,nodeid=2,memdev=mid",
                                         "node,nodeid=3,memdev=disk", "node,nodeid=4,memdev=high"};

    for (const auto &node : nodes) {
        launch.options.insert(launch.options.end(), {"-numa", node});
    }

    if (engine != QnxEngine::hvf) {
        launch.options.insert(launch.options.end(), tcg_clock.begin(), tcg_clock.end());
    }

    return launch;
}

void create_qnx_ram(const std::filesystem::path &path, std::uint64_t bytes) {
    if ((bytes != (8ULL << 30) && bytes != (11713ULL << 20)) || !path.is_absolute() ||
        path.lexically_normal() != path || path.filename() != "qnx-ram.bin" ||
        std::filesystem::canonical(path.parent_path()) != path.parent_path()) {
        throw VoyemuError("QNX_RAM_CREATE",
                          "exact guest extent and canonical session RAM path required");
    }

#ifdef _WIN32
    detail::PrivateStateSecurity security;
    const HANDLE file = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE | DELETE,
                                    FILE_SHARE_READ | FILE_SHARE_WRITE, &security.attributes,
                                    CREATE_NEW,
                                    FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);

    if (file == INVALID_HANDLE_VALUE) {
        throw VoyemuError("QNX_RAM_CREATE", "exclusive sparse RAM creation failed");
    }

    DWORD returned = 0;
    LARGE_INTEGER length{};
    length.QuadPart = static_cast<LONGLONG>(bytes);
    const bool sparse = DeviceIoControl(file, FSCTL_SET_SPARSE, nullptr, 0, nullptr, 0, &returned,
                                        nullptr) != 0;
    const bool okay = sparse && SetFilePointerEx(file, length, nullptr, FILE_BEGIN) &&
                      SetEndOfFile(file) && FlushFileBuffers(file);

    if (!okay) {
        FILE_DISPOSITION_INFO disposition{TRUE};
        SetFileInformationByHandle(file, FileDispositionInfo, &disposition, sizeof(disposition));
    }

    CloseHandle(file);

    if (!okay) {
        throw VoyemuError("QNX_RAM_CREATE", "sparse RAM extent creation failed");
    }
#else
    const int fd = ::open(path.c_str(), O_RDWR | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);

    if (fd < 0) {
        throw VoyemuError("QNX_RAM_CREATE", "exclusive sparse RAM creation failed");
    }

    const bool okay = ::ftruncate(fd, static_cast<off_t>(bytes)) == 0 && ::fsync(fd) == 0;
    ::close(fd);

    if (!okay) {
        ::unlink(path.c_str());
        throw VoyemuError("QNX_RAM_CREATE", "sparse RAM extent creation failed");
    }
#endif
}

bool qnx_ram_clock_options_current(QnxEngine engine, const std::vector<std::string> &arguments) {
    const auto clock = [](const std::vector<std::string> &values) {
        std::vector<std::string> result;

        for (std::size_t i = 0; i < values.size(); ++i) {
            if (values[i] == "-icount") {
                result.push_back(values[i]);

                if (i + 1 < values.size()) {
                    result.push_back(values[++i]);
                }
            }
        }

        return result;
    };

    return clock(arguments) == clock(qnx_ram_launch(engine).options);
}
} // namespace voyemu
