#include "qnx_process_ram.hpp"
#include "os/platform.hpp"
#include "process.hpp"
#include "qnx_persistent_storage.hpp"
#include "qnx_process_ram_test_host.hpp"
#include <algorithm>
#include <cassert>
#include <fstream>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
using namespace voyemu;

int main() try {
    diagnostic_persistent_storage_case();
    const std::string engine_pin(64, "a"[0]);
    const Json engine_capability = {
            {"schema", 1}, {"version", "11.1.2"}, {"engine_sha256", engine_pin}};
    assert(qnx_file_backend_supported(engine_capability, engine_pin));

    for (const auto &invalid : {Json(), Json::array(), Json::object()}) {
        assert(!qnx_file_backend_supported(invalid, engine_pin));
    }

    auto invalid_capability = engine_capability;
    invalid_capability["schema"] = "1";
    assert(!qnx_file_backend_supported(invalid_capability, engine_pin));
    invalid_capability = engine_capability;
    invalid_capability["version"] = true;
    assert(!qnx_file_backend_supported(invalid_capability, engine_pin));
    invalid_capability = engine_capability;
    invalid_capability["engine_sha256"] = 7;
    assert(!qnx_file_backend_supported(invalid_capability, engine_pin));
    invalid_capability = engine_capability;
    invalid_capability["extra"] = true;
    assert(!qnx_file_backend_supported(invalid_capability, engine_pin));
    const auto tcg = qnx_ram_launch(QnxEngine::tcg_file), mac = qnx_ram_launch(QnxEngine::hvf);
    const std::vector<std::string> clock{"-icount", "shift=0,align=off,sleep=on"};
    const auto passion_mac = qnx_ram_launch(QnxEngine::hvf, 11713),
               passion_tcg = qnx_ram_launch(QnxEngine::tcg_file, 11713);
    std::vector<std::string> passion_tcg_options = passion_mac.options;
    passion_tcg_options.insert(passion_tcg_options.end(), clock.begin(), clock.end());
    assert(passion_mac.options ==
                   std::vector<std::string>(
                           {"-object",
                            "memory-backend-file,id=qnxmem,size=11713M,mem-path=${SESSION}/qnx-ram.bin,share=on"}) &&
           passion_tcg.options == passion_tcg_options);

    for (const auto memory : {0u, 8191u, 8193u, 11712u, 11714u}) {
        bool denied = false;
        try {
            qnx_ram_launch(QnxEngine::hvf, memory);
        } catch (const std::runtime_error &) {
            denied = true;
        }

        assert(denied);
    }

    assert(mac.backend == "file" && mac.machine.ends_with("memory-backend=qnxmem") &&
           mac.options ==
                   std::vector<std::string>(
                           {"-object",
                            "memory-backend-file,id=qnxmem,size=8192M,mem-path=${SESSION}/qnx-ram.bin,share=on"}));
    assert(tcg.accelerator == "tcg,thread=single" && mac.accelerator == "hvf,kernel-irqchip=on");
    assert(std::find(mac.options.begin(), mac.options.end(), "-icount") == mac.options.end());
    assert(qnx_ram_clock_options_current(QnxEngine::tcg_file,
                                         {"-cpu", "max", "-icount", "shift=0,align=off,sleep=on"}));

    for (const auto &stale :
         std::vector<std::vector<std::string>>{{},
                                               {"-icount"},
                                               {"-icount", "shift=auto"},
                                               {"-icount", "shift=0,align=off,sleep=on", "-icount",
                                                "shift=0,align=off,sleep=on"}}) {
        assert(!qnx_ram_clock_options_current(QnxEngine::tcg_file, stale));
    }

    assert(qnx_ram_clock_options_current(QnxEngine::hvf, {"-accel", "hvf,kernel-irqchip=on"}));
    assert(!qnx_ram_clock_options_current(QnxEngine::hvf,
                                          {"-icount", "shift=0,align=off,sleep=on"}));
    const auto persistent = qnx_persistent_ram_launch(
            QnxEngine::hvf, "/machines/car/qnx/system.img", "/machines/car/qnx/userdata.img");
    assert(persistent.backend == "file" &&
           persistent.machine.find("memory-backend") == std::string::npos &&
           persistent.options.size() == 20);
    assert(persistent.options[1] ==
           "memory-backend-file,id=low,size=1128M,mem-path=${SESSION}/qnx-ram.bin,offset=0,share=on");
    assert(persistent.options[3] ==
           "memory-backend-file,id=user,size=32M,mem-path=/machines/car/qnx/userdata.img,share=on");
    assert(persistent.options[5] ==
           "memory-backend-file,id=mid,size=888M,mem-path=${SESSION}/qnx-ram.bin,offset=1216348160,share=on");
    assert(persistent.options[7] ==
           "memory-backend-file,id=disk,size=1025M,mem-path=/machines/car/qnx/system.img,share=on");
    assert(persistent.options[9] ==
           "memory-backend-file,id=high,size=5119M,mem-path=${SESSION}/qnx-ram.bin,offset=3222274048,share=on");
    assert(persistent.options[11] == "node,nodeid=0,cpus=0-7,memdev=low" &&
           persistent.options[19] == "node,nodeid=4,memdev=high");

    if (voyemu::os::host_windows) {
        // The Windows engine maps the RAM file through its own file backend, which must be
        // verified.
        bool unsupported = false;
        try {
            qnx_persistent_ram_launch(QnxEngine::tcg_file, "/system", "/userdata");
        } catch (const std::runtime_error &error) {
            unsupported = std::string(error.what()).starts_with("QNX_PERSISTENCE_UNSUPPORTED:");
        }

        assert(unsupported);
    }

    const auto windows_persistent =
            qnx_persistent_ram_launch(QnxEngine::tcg_file, "/machines/car/qnx/system.img",
                                      "/machines/car/qnx/userdata.img", true);
    assert(windows_persistent.backend == "file" &&
           windows_persistent.accelerator == "tcg,thread=single" &&
           windows_persistent.options.size() == 22);
    assert(std::equal(persistent.options.begin(), persistent.options.end(),
                      windows_persistent.options.begin()));
    assert(windows_persistent.options[20] == "-icount" &&
           windows_persistent.options[21] == "shift=0,align=off,sleep=on");

    // Linux and Windows run TCG with the clock and keep the RAM file of the HVF launch.
    std::vector<std::string> tcg_options = mac.options;
    tcg_options.insert(tcg_options.end(), clock.begin(), clock.end());
    assert(tcg.backend == "file" && tcg.machine == mac.machine && tcg.options == tcg_options);
    assert(qnx_ram_clock_options_current(QnxEngine::tcg_file, tcg.options) &&
           !qnx_ram_clock_options_current(QnxEngine::tcg_file, mac.options));
    assert(std::string(qnx_engine_cpu(QnxEngine::hvf)) == "host" &&
           std::string(qnx_engine_cpu(QnxEngine::tcg_file)) == "max");

    for (const auto &path : {"", "/comma,path", "/newline\npath"}) {
        bool invalid = false;
        try {
            qnx_persistent_ram_launch(QnxEngine::hvf, path, "/userdata");
        } catch (const std::runtime_error &) {
            invalid = true;
        }

        assert(invalid);
    }

    persistent_storage_case();
} catch (const std::exception &error) {
    std::cerr << error.what() << '\n';

    return 1;
}
