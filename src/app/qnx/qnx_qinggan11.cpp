#include "qnx_qinggan11.hpp"
#include "check.hpp"
#include "error.hpp"
#include "process.hpp"
#include "qnx_imagefs.hpp"
#include <cctype>
#include <stdexcept>
namespace voyemu {
void validate_qnx_qinggan11_ram_driver(QnxGuestAdapter adapter, std::string_view driver) {
    const auto &spec = qnx_adapter(adapter);

    if (driver.size() != 22165) {
        throw VoyemuError("QNX_QINGGAN11_RAM_DRIVER_SIZE",
                          "OEM image differs from the expected layout");
    }

    // Preserve OEM bytes: this verifies code and relocations without rewriting the loader metadata.
    if (spec.ram_driver_paddr != std::array<std::uint8_t, 2>{} &&
        (static_cast<unsigned char>(driver[201]) != spec.ram_driver_paddr[0] ||
         static_cast<unsigned char>(driver[257]) != spec.ram_driver_paddr[1])) {
        throw VoyemuError("QNX_QINGGAN11_RAM_DRIVER_ADDRESS",
                          "OEM image differs from the expected layout");
    }
}

std::string prepare_qnx_qinggan11_theme_scene(QnxGuestAdapter adapter, std::string_view original) {
    if (original.size() != qnx_adapter(adapter).theme_scene_bytes) {
        throw VoyemuError("QNX_QINGGAN11_THEME_SCENE_SIZE",
                          "OEM image differs from the expected layout");
    }

    std::string output(original);

    for (const auto at : {0x9de4, 0x9ed4, 0xa2cc, 0xa3b4}) {
        if (original.substr(at, 4) != std::string_view("\x5f\x4c\x00\x71", 4)) {
            throw VoyemuError("QNX_QINGGAN11_THEME_SCENE_COMPARATOR",
                              "OEM image differs from the expected layout");
        }

        output[at + 1] = static_cast<char>(0xcc);
    }

    return output;
}

std::string prepare_qnx_qinggan11_theme_manager(QnxGuestAdapter adapter,
                                                std::string_view original) {
    // The pinned manager sends to MID 1/2/3/12; MID 12 sets bit 0x20.
    // Require its genuine response too, retaining the OEM transitions and timers.
    if (original.size() != qnx_adapter(adapter).theme_manager_bytes) {
        throw VoyemuError("QNX_QINGGAN11_THEME_MANAGER_SIZE",
                          "OEM image differs from the expected layout");
    }

    std::string output(original);

    for (const auto at : {0x5844, 0x587c, 0x58ec, 0x5924, 0x59b0}) {
        if (original.substr(at, 4) != std::string_view("\x1f\x4c\x00\x71", 4)) {
            throw VoyemuError("QNX_QINGGAN11_THEME_MANAGER_COMPARATOR",
                              "OEM image differs from the expected layout");
        }

        output[at + 1] = static_cast<char>(0xcc);
    }

    return output;
}

std::string prepare_qnx_qinggan11_runtime(std::string_view source, std::string_view hash) {
    // Free uses the original fractional sleep; project out only the pinned Windows opt-in.
    // A9 startup tracing is observational; retain the accepted Free runtime bytes.
    std::string without_trace(source);
    unsigned trace_lines = 0;

    for (const auto marker :
         {"echo 'QMC WAIT initial_sleep before'\n", "echo 'QMC WAIT initial_sleep after'\n",
          "echo 'QMC WAIT ram_driver before'\n", "echo 'QMC WAIT ram_driver after'\n"}) {
        const auto at = without_trace.find(marker);

        if (at == std::string::npos) {
            continue;
        }

        if (without_trace.find(marker, at + 1) != std::string::npos) {
            throw VoyemuError("QNX_QINGGAN11_RUNTIME_WAIT_TRACE",
                              "OEM image differs from the expected layout");
        }

        without_trace.erase(at, std::string_view(marker).size());
        ++trace_lines;
    }

    if (trace_lines != 0 && trace_lines != 4) {
        throw VoyemuError("QNX_QINGGAN11_RUNTIME_WAIT_TRACE",
                          "OEM image differs from the expected layout");
    }

    source = without_trace;
    const std::string
            initial_begin = "qm_initial_native_delay() (\n",
            initial_end = "qm_wait_ram_driver() {\n",
            initial_case = R"QNX_INITIAL(case "${VOYEMU_QNX_INITIAL_NATIVE_DELAY-}" in
    '') if ! /bin/sleep 0.01; then echo 'QMC FAIL fractional-sleep-required'; exit 1; fi ;;
    1) if ! qm_initial_native_delay; then echo 'QMC FAIL initial-native-delay'; exit 125; fi ;;
    *) echo 'QMC FAIL initial-delay-mode'; exit 125 ;;
esac
)QNX_INITIAL",
            initial_default =
                    R"QNX_INITIAL(if ! /bin/sleep 0.01; then echo 'QMC FAIL fractional-sleep-required'; exit 1; fi
)QNX_INITIAL";
    const auto initial_at = source.find(initial_begin), initial_after = source.find(initial_end),
               initial_case_at = source.find(initial_case);

    if (initial_at == std::string_view::npos || initial_after == std::string_view::npos ||
        initial_after <= initial_at || initial_case_at == std::string_view::npos ||
        source.find(initial_begin, initial_at + 1) != std::string_view::npos ||
        source.find(initial_end, initial_after + 1) != std::string_view::npos ||
        source.find(initial_case, initial_case_at + 1) != std::string_view::npos) {
        throw VoyemuError("QNX_QINGGAN11_RUNTIME_INITIAL_DELAY",
                          "OEM image differs from the expected layout");
    }

    std::string selected_source(source);
    selected_source.erase(initial_at, initial_after - initial_at);
    selected_source.replace(selected_source.find(initial_case), initial_case.size(),
                            initial_default);

    for (const auto token :
         {"qm_initial_native_delay", "VOYEMU_QNX_INITIAL_NATIVE_DELAY", "initial-delay10ms-v1"}) {
        if (selected_source.find(token) != std::string::npos) {
            throw VoyemuError("QNX_QINGGAN11_RUNTIME_INITIAL_DELAY",
                              "OEM image differs from the expected layout");
        }
    }

    source = selected_source;
    const std::string marker = "\nqm_source_capture=0\n",
                      old = "2e13e9b8299f882414400969309c501c639314a92f5d0e603806e0f13d37426c";
    const auto at = source.find(marker);

    if (!source.starts_with("#!/proc/boot/bin/ksh\n") || at == std::string_view::npos ||
        source.find(marker, at + 1) != std::string_view::npos || !sha256_text(hash)) {
        throw VoyemuError("QNX_QINGGAN11_RUNTIME_SOURCE",
                          "OEM image differs from the expected layout");
    }

    const std::string copy = "of=/log/mcd-observer-padded.so bs=16928 count=1";
    const auto copy_at = source.find(copy);

    if (copy_at == std::string_view::npos ||
        source.find(copy, copy_at + 1) != std::string_view::npos) {
        throw VoyemuError("QNX_QINGGAN11_RUNTIME_OBSERVER_COPY",
                          "OEM image differs from the expected layout");
    }

    unsigned count = 0;

    for (std::size_t p = 0; (p = source.find(old, p)) != std::string_view::npos; p += 64) {
        ++count;
    }

    if (count != 2) {
        throw VoyemuError("QNX_QINGGAN11_RUNTIME_OBSERVER",
                          "OEM image differs from the expected layout");
    }

    std::string output(source);
    output[at + marker.find('0')] = '1';
    output.replace(copy_at, copy.size(), "of=/log/mcd-observer-padded.so bs=21312 count=1");

    for (std::size_t p = 0; (p = output.find(old, p)) != std::string::npos; p += 64) {
        output.replace(p, 64, hash);
    }

    // Nested IFS utilities mount at /proc/boot; the host sleep remains at /bin.
    constexpr std::array<std::pair<std::string_view, unsigned>, 13> utilities{
            {{"/bin/sleep", 13},
             {"/bin/cat", 9},
             {"/bin/dd", 2},
             {"/bin/grep", 8},
             {"/bin/ls", 8},
             {"/bin/openssl", 3},
             {"/bin/chmod", 1},
             {"/sbin/mkqnx6fs", 1},
             {"/bin/mount", 1},
             {"/bin/mkdir", 4},
             {"/bin/touch", 4},
             {"/bin/ln", 4},
             {"/bin/pps", 1}}};
    auto path_character = [](char c) {
        return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '/' || c == '.' ||
               c == '-';
    };

    for (const auto &[path, expected] : utilities) {
        unsigned found = 0;

        for (std::size_t p = 0; (p = output.find(path, p)) != std::string::npos;) {
            const auto end = p + path.size();

            if ((p && path_character(output[p - 1])) ||
                (end < output.size() && path_character(output[end]))) {
                p = end;
                continue;
            }

            if (path == "/bin/sleep") {
                p = end;
            } else {
                output.insert(p, "/proc/boot");
                p = end + 10;
            }

            ++found;
        }

        if (found != expected) {
            throw VoyemuError("QNX_QINGGAN11_RUNTIME_UTILITY_COUNT",
                              "OEM image differs from the expected layout");
        }
    }

    for (const auto prefix : {"/bin/", "/sbin/"}) {
        for (std::size_t p = 0; (p = output.find(prefix, p)) != std::string::npos; ++p) {
            if (p == 0 || !path_character(output[p - 1])) {
                if (output.substr(p, 10) == "/bin/sleep" &&
                    (p + 10 == output.size() || !path_character(output[p + 10]))) {
                    continue;
                }

                throw VoyemuError("QNX_QINGGAN11_RUNTIME_UTILITY_UNKNOWN",
                                  "OEM image differs from the expected layout");
            }
        }
    }

    if (output.find("/proc/boot/bin/sleep") != std::string::npos) {
        throw VoyemuError("QNX_QINGGAN11_RUNTIME_HOST_SLEEP",
                          "OEM image differs from the expected layout");
    }

    const std::string launch =
            "/usr/bin/dlt-daemon -c /log/dlt-owned.conf > /log/dlt-ascii.log 2>&1 &";
    const auto launch_at = output.find(launch);

    if (launch_at == std::string::npos || output.find(launch, launch_at + 1) != std::string::npos) {
        throw VoyemuError("QNX_QINGGAN11_RUNTIME_DLT_LAUNCH",
                          "OEM image differs from the expected layout");
    }

    output.insert(
            launch_at,
            "if [ ! -e /dev/socket ]; then\n    /proc/boot/bin/io-pkt-v6-hc -I -t2 > /log/io-pkt-local.log 2>&1 &\n    qm_socket_provider=$!\n    qm_bounded socket-provider /proc/boot/bin/waitfor /dev/socket 5 || { qm_bounded socket-stderr /proc/boot/bin/dd if=/log/io-pkt-local.log bs=1024 count=8; exit 1; }\nfi\n");
    const std::string dlt = "qm_bounded dlt-ready qm_wait_dlt || exit 1";
    const auto dlt_at = output.find(dlt);

    if (dlt_at == std::string::npos || output.find(dlt, dlt_at + 1) != std::string::npos) {
        throw VoyemuError("QNX_QINGGAN11_RUNTIME_DLT_MARKER",
                          "OEM image differs from the expected layout");
    }

    output.replace(
            dlt_at, dlt.size(),
            "qm_bounded dlt-ready qm_wait_dlt || { qm_bounded dlt-stderr /proc/boot/bin/dd if=/log/dlt-ascii.log bs=1024 count=8; exit 1; }");
    const std::string network_anchor =
            "/usr/bin/dlt-daemon -c /log/dlt-owned.conf > /log/dlt-ascii.log 2>&1 &";
    const auto network_at = output.find(network_anchor),
               socket_at = output.find(
                       "qm_bounded socket-provider /proc/boot/bin/waitfor /dev/socket 5");

    if (network_at == std::string::npos ||
        output.find(network_anchor, network_at + 1) != std::string::npos ||
        socket_at == std::string::npos || socket_at > network_at ||
        output.find("VOYEMU_QNET_RELAY=") != std::string::npos) {
        throw VoyemuError("QNX_QINGGAN11_RUNTIME_NETWORK",
                          "OEM image differs from the expected layout");
    }

    output.insert(network_at, R"QNET(/usr/sbin/random -t >&2 &
qm_random=$!
qm_bounded random-dev /proc/boot/bin/waitfor /dev/random 5 || exit 1
qm_bounded urandom-dev /proc/boot/bin/waitfor /dev/urandom 5 || exit 1
kill -0 "$qm_random" && [ -c /dev/random ] && [ -c /dev/urandom ] || exit 1
qm_bounded random-ready /proc/boot/bin/openssl rand -out /dev/null 32 || exit 1
qm_bounded net-create /bin/ifconfig tap0 create || exit 1
qm_bounded net-up /bin/ifconfig tap0 172.16.100.22 netmask 255.255.255.0 up || exit 1
VOYEMU_QNET_DIAGNOSTICS=1 VOYEMU_QNET_RELAY=1 LD_PRELOAD=/proc/boot/bin/Snapdragon.rgb /proc/boot/bin/ls >&2 &
qm_qnet=$!
/usr/sbin/qconn >&2 &
qm_qconn=$!
)QNET");

    return output;
}

std::string prepare_qnx_qinggan11_display_config(std::string_view image,
                                                 std::string_view configuration) {
    if (configuration.empty() || configuration.size() > 1119) {
        throw VoyemuError("QNX_QINGGAN11_GRAPHICS_CAPACITY",
                          "OEM image differs from the expected layout");
    }

    const auto unique = rename_qnx_imagefs_duplicate_file(
            image, "lib64/graphics.conf", "lib64/graphics.old",
            "c02066b2f5d7e0dc14d21ad1911baf53fa289d93f2c4a2c60644b3428d81d178");

    return replace_qnx_imagefs_file(
            unique, "lib64/graphics.conf",
            sha256_bytes(read_qnx_imagefs_file(unique, "lib64/graphics.conf")), configuration, 1,
            true);
}
} // namespace voyemu
