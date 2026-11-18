#pragma once
#include "model.hpp"
namespace voyemu {
const Json &embedded_profile_catalog();
const Json &embedded_profile(std::string_view id);
std::vector<std::string> embedded_profile_names();
// Declared patch dependencies only; component build recipes are a separate graph.
Json declared_patch_plan(const Json &definition);
} // namespace voyemu
