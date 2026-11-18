#pragma once
#include "can_observation.hpp"
namespace voyemu {
void validate_can_model_request(const Json &request);
Json parse_can_model_observation(const std::string &output, int exit_code, const Json &request);
} // namespace voyemu
namespace voyemu {
Json collect_can_model_observation(const Json &request, const CanGuestIdentity &identity,
                                   const std::string &installed_sha256, int timeout_ms,
                                   const CanGuestRunner &runner);
}
