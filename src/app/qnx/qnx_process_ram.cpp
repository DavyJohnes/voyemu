#include "qnx_process_ram.hpp"
#include "error.hpp"
#include "os/fs.hpp"
#include <array>
#include <stdexcept>
#include <string>
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

    switch (os::create_sparse_file(path, bytes)) {
    case os::SparseFile::created:
        break;
    case os::SparseFile::create_failed:
        throw VoyemuError("QNX_RAM_CREATE", "exclusive sparse RAM creation failed");
    case os::SparseFile::extent_failed:
        throw VoyemuError("QNX_RAM_CREATE", "sparse RAM extent creation failed");
    }
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
