#pragma once
#include "model.hpp"
namespace voyemu::fetch_detail {
void bind_owner_inputs(const std::filesystem::path &root, std::string_view profile,
                       const Json &definition, const Json &imported, Json &result);
} // namespace voyemu::fetch_detail
