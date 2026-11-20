#pragma once
#include "error.hpp"
#include <atomic>

namespace voyemu::interruption {
// Set by SIGINT and SIGTERM while a preparation command (deps, fetch or build) runs; sessions and
// their supervisor keep their own stop flag.
inline std::atomic<bool> requested = false;

// Throws INTERRUPTED once requested; downloads, extraction, hashing and imports call it for each
// block, so an interrupted command stops promptly and removes its owned partial outputs while the
// exception unwinds.
inline void check() {
    if (requested) {
        throw VoyemuError("INTERRUPTED", "command interrupted");
    }
}
} // namespace voyemu::interruption
