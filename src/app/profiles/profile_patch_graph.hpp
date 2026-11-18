#pragma once
#include "model.hpp"
namespace voyemu {
// Source-bound planning only. Ordered typed dispatch records never invoke TS/Java.
// Complete patch coverage and a ready machine are different: machine_ready is always false here.
Json plan_profile_patch_graph(const std::filesystem::path &root, std::string_view profile);
} // namespace voyemu
