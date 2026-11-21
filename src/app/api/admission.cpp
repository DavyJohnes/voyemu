#include "admission.hpp"
#include "admission_store.hpp"
#include "error.hpp"
#include "process.hpp"

namespace voyemu::api {
namespace admission {
void fail() {
    throw VoyemuError("ADMISSION_STATE", "private receipt unavailable");
}
} // namespace admission

namespace {
using admission::fail;
using admission::record_limit;
using admission::Reservation;

constexpr std::size_t payload_limit = 128 * 1024;

std::string digest(const std::string &text) {
    return sha256_bytes(text);
}

std::string key(const std::string &scope, const std::string &id) {
    if (scope.empty() || id.empty() || scope.size() > 4096 || id.size() > 4096) {
        throw VoyemuError("INVALID_REQUEST", "invalid admission key");
    }

    return digest(Json::array({scope, id}).dump());
}

void atomic_state(Reservation &reservation, const Json &state) {
    const auto text = state.dump();

    if (text.size() > record_limit) {
        throw VoyemuError("ADMISSION_EXTENT", "receipt exceeds 8 MiB");
    }

    reservation.write_state(text);
}

AdmissionResult existing(Reservation &lock, const std::string &name,
                         const std::optional<std::string> &expected) {
    AdmissionResult result{false, name.substr(0, 32), "unknown", std::nullopt};
    auto raw = lock.read_reservation();
    Json reservation;

    try {
        reservation = Json::parse(raw);
    } catch (const Json::exception &) {
        return result;
    }

    if (!reservation.is_object() || reservation.value("version", 0) != 1 ||
        !reservation.contains("digest") || !reservation["digest"].is_string()) {
        return result;
    }

    if (expected && reservation["digest"] != *expected) {
        throw VoyemuError("REQUEST_CONFLICT", "request_id already used with a different payload");
    }

    result.status = "pending";
    const auto text = lock.read_state();

    if (!text) {
        return result;
    }

    try {
        auto state = Json::parse(*text);

        if (state.at("operation_id") != result.operation_id ||
            state.at("digest") != reservation["digest"]) {
            fail();
        }

        if (state.at("status") == "terminal") {
            result.response = state.at("response");
            result.status = "terminal";
        } else if (state.at("status") != "pending") {
            fail();
        }
    } catch (...) {
        fail();
    }

    return result;
}
} // namespace

AdmissionStore::AdmissionStore(std::filesystem::path directory)
    : directory_(std::filesystem::absolute(directory).lexically_normal()) {
    admission::open_store_directory(directory_, true);
}

AdmissionResult AdmissionStore::begin(const std::string &scope, const std::string &request_id,
                                      const Json &payload) {
    auto name = key(scope, request_id);
    auto canonical = payload.dump();

    if (canonical.size() > payload_limit) {
        throw VoyemuError("INVALID_REQUEST", "action payload too large");
    }

    auto hash = digest(canonical);
    Reservation lock(directory_, name, true);

    if (!lock.created()) {
        return existing(lock, name, hash);
    }

    lock.write_reservation(Json{{"version", 1}, {"digest", hash}}.dump());
    atomic_state(
            lock,
            Json{{"operation_id", name.substr(0, 32)}, {"digest", hash}, {"status", "pending"}});

    return {true, name.substr(0, 32), "pending", std::nullopt};
}

std::optional<AdmissionResult> AdmissionStore::lookup(const std::string &scope,
                                                      const std::string &request_id) {
    const auto name = key(scope, request_id);

    if (!admission::reservation_exists(directory_, name)) {
        return std::nullopt;
    }

    Reservation lock(directory_, name, false);

    return existing(lock, name, std::nullopt);
}

void AdmissionStore::complete(const std::string &scope, const std::string &request_id,
                              const std::string &operation_id, const Json &response) {
    auto name = key(scope, request_id);
    Reservation lock(directory_, name, false);
    auto result = existing(lock, name, std::nullopt);

    if (result.operation_id != operation_id || result.status == "unknown") {
        fail();
    }

    if (result.response) {
        if (*result.response != response) {
            fail();
        }

        return;
    }

    auto reservation = Json::parse(lock.read_reservation());
    atomic_state(lock, Json{{"operation_id", operation_id},
                            {"digest", reservation.at("digest")},
                            {"status", "terminal"},
                            {"response", response}});
}
} // namespace voyemu::api
