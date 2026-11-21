#pragma once
#include "model.hpp"
#include "profile_identity.hpp"
namespace voyemu {
// Every bundled definition declares a valid platform, board and title.
const Json &embedded_profile_catalog();
const Json &embedded_profile(std::string_view id);
std::vector<std::string> embedded_profile_names();
// The bundled profiles that declare this platform and board, in catalog order.
std::vector<std::string> embedded_profile_names(ProfilePlatform platform, ProfileBoard board);
// Whether the bundled profile declares this platform and board; an id outside the catalog, such as
// a local copy, declares none.
bool embedded_profile_declares(std::string_view id, ProfilePlatform platform, ProfileBoard board);
// Whether a profile definition or a machine launch record declares this platform and board; a
// record without a platform declares none.
bool record_declares(const Json &record, ProfilePlatform platform, ProfileBoard board);
// Declared patch dependencies only; component build recipes are a separate graph.
Json declared_patch_plan(const Json &definition);
} // namespace voyemu
