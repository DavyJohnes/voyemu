#pragma once
// The private files of AdmissionStore behind one interface; admission_posix.cpp and
// admission_windows.cpp implement them for their host.
#include <cstddef>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>

namespace voyemu::api::admission {
// The largest record of the store.
inline constexpr std::size_t record_limit = 8 * 1024 * 1024;

[[noreturn]] void fail();
// Opens the private store directory, creating it when create is set; throws on a foreign or linked
// directory.
void open_store_directory(const std::filesystem::path &store, bool create);
// Whether the store holds the reservation of name; throws on any other failure than its absence.
bool reservation_exists(const std::filesystem::path &store, const std::string &name);

// The reservation of one request, held under its exclusive lock while the object lives.
class Reservation {
public:
    // Creates the reservation when create is set and none exists, else opens the existing one.
    Reservation(const std::filesystem::path &store, const std::string &name, bool create);
    ~Reservation();
    Reservation(const Reservation &) = delete;
    Reservation &operator=(const Reservation &) = delete;
    // Whether this object created the reservation.
    bool created() const;
    std::string read_reservation();
    void write_reservation(const std::string &text);
    // The state record of the request, none before the first one.
    std::optional<std::string> read_state();
    // Replaces the state record atomically and durably.
    void write_state(const std::string &text);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace voyemu::api::admission
