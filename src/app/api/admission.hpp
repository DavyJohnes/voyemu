#pragma once
#include "model.hpp"
#include <optional>

namespace voyemu::api {
struct AdmissionResult {
    bool admitted = false;
    std::string operation_id;
    std::string status;
    std::optional<Json> response;
};
// Private durable action receipts. No lock survives a call to this store.
class AdmissionStore {
public:
    explicit AdmissionStore(std::filesystem::path directory);
    AdmissionResult begin(const std::string &scope, const std::string &request_id,
                          const Json &payload);
    std::optional<AdmissionResult> lookup(const std::string &scope, const std::string &request_id);
    void complete(const std::string &scope, const std::string &request_id,
                  const std::string &operation_id, const Json &response);

private:
    std::filesystem::path directory_;
};
} // namespace voyemu::api
