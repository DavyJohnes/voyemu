#pragma once
#include "model.hpp"
#include <array>
#include <functional>
#include <stdexcept>
namespace voyemu {
struct HostRefreshRecord {
    std::filesystem::path path;
    std::string before, after;
};

// Release recovery only after every metadata record and the payload are restored.
template <std::size_t N>
inline void
rollback_host_refresh_payload(const std::array<HostRefreshRecord, N> &records,
                              const std::function<std::string(const std::filesystem::path &)> &read,
                              const std::function<void()> &restore_payload, bool &retained) {
    retained = true;
    const auto restored = [&] {
        for (const auto &record : records) {
            if (!record.path.empty() && read(record.path) != record.before) {
                throw VoyemuError("HOST_REFRESH_ROLLBACK",
                                  "foreign/unverifiable metadata; recovery retained");
            }
        }
    };
    restored();
    restore_payload();
    restored();
    retained = false;
}

// Only the three host records, plus an optional cluster launch, participate.
template <std::size_t N>
    requires(N == 3 || N == 4)

inline void publish_host_refresh(
        const std::array<HostRefreshRecord, N> &records,
        const std::function<std::string(const std::filesystem::path &)> &read,
        const std::function<void(const std::filesystem::path &, const std::string &)> &write,
        const std::function<void()> &validate) {
    auto need = [](bool yes) {
        if (!yes) {
            throw VoyemuError("HOST_REFRESH_CAS", "host binding bytes changed");
        }
    };
    unsigned attempted = 0;

    try {
        for (const auto &record : records) {
            if (!record.path.empty()) {
                need(read(record.path) == record.before);
            }
        }

        validate();

        for (unsigned i = 0; i < records.size(); ++i) {
            const auto &record = records[i];
            attempted = i + 1;

            if (record.path.empty()) {
                continue;
            }

            for (unsigned j = 0; j < records.size(); ++j) {
                if (!records[j].path.empty()) {
                    need(read(records[j].path) == (j < i ? records[j].after : records[j].before));
                }
            }

            write(record.path, record.after);
        }

        for (const auto &record : records) {
            if (!record.path.empty()) {
                need(read(record.path) == record.after);
            }
        }

        validate();
    } catch (...) {
        const auto original = std::current_exception();
        bool incomplete = false;

        while (attempted) {
            const auto &record = records[--attempted];

            if (record.path.empty()) {
                continue;
            }

            try {
                const auto current = read(record.path);

                if (current == record.after) {
                    write(record.path, record.before);
                } else if (current != record.before) {
                    incomplete = true;
                }
            } catch (...) {
                incomplete = true;
            }
        }

        if (incomplete) {
            throw VoyemuError(
                    "HOST_REFRESH_ROLLBACK",
                    "foreign/unverifiable binding retained; other owned publications rolled back");
        }

        std::rethrow_exception(original);
    }
}
} // namespace voyemu
