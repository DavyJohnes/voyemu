#pragma once
#include "qnx_persistent_storage.hpp"
#include <chrono>
#include <optional>
namespace voyemu {
// The measured x86 TCG cold boot reaches ACC after 8646 s; keep a finite 3 h ceiling.
inline unsigned qnx_source_acceptance_seconds(QnxEngine engine) {
    return engine == QnxEngine::hvf ? 90u : 10800u;
}

// The host time of guest work that takes `hvf` under HVF: TCG runs the guest slower by the ratio
// of the source acceptance budgets.
template <class Rep, class Period>
std::chrono::duration<Rep, Period> qnx_guest_deadline(QnxEngine engine,
                                                      std::chrono::duration<Rep, Period> hvf) {
    return hvf * qnx_source_acceptance_seconds(engine) /
           qnx_source_acceptance_seconds(QnxEngine::hvf);
}

// The guest sync before a cluster closes: 5 s under HVF.
inline std::chrono::milliseconds qnx_guest_sync(QnxEngine engine = host_qnx_engine) {
    return qnx_guest_deadline(engine, std::chrono::milliseconds(5000));
}

// The time a request that closes a cluster (stop, cluster_power, cluster_restart) adds to its
// HVF bound for the guest sync of the engine.
inline std::chrono::milliseconds qnx_cluster_close_extra(QnxEngine engine = host_qnx_engine) {
    return qnx_guest_sync(engine) - qnx_guest_sync(QnxEngine::hvf);
}

// Past the hour a Windows run needs the verified file backend of the pinned engine; QEMU elsewhere
// maps the RAM file natively.
inline bool qnx_cluster_seconds_valid(QnxEngine engine, std::optional<unsigned> seconds,
                                      const Json &specification,
                                      bool file_backend_required = os::host_windows) {
    if (!seconds) {
        return true;
    }

    if (*seconds < 1) {
        return false;
    }

    if (*seconds <= 3600) {
        return true;
    }

    if (engine == QnxEngine::hvf || *seconds > qnx_source_acceptance_seconds(engine)) {
        return false;
    }

    try {
        if (!specification.contains("source_acceptance") ||
            !specification.at("source_acceptance").is_boolean() ||
            specification.at("source_acceptance") != true) {
            return false;
        }

        const auto executable = specification.at("qemu").at("executable").get<std::string>();
        const auto pin = sha256_file(executable);
        unsigned matches = 0;

        for (const auto &item : specification.at("pins")) {
            if (item.at("path") == executable) {
                if (item.at("sha256") != pin) {
                    return false;
                }

                ++matches;
            }
        }

        return matches == 1 &&
               (!file_backend_required ||
                qnx_file_backend_supported(specification.at("qnx_file_backend"), pin));
    } catch (const std::exception &) {
        return false;
    }
}
} // namespace voyemu
