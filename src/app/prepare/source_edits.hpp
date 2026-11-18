#pragma once
#include "model.hpp"
namespace voyemu {
// Version-1 edits use 1-based coordinates in the verified original byte snapshot.
std::string assemble_source_edits(std::string_view original, const Json &edits);
Json materialize_native_component_sources(const std::filesystem::path &source,
                                          const std::filesystem::path &source_cache,
                                          const std::filesystem::path &destination);
} // namespace voyemu
