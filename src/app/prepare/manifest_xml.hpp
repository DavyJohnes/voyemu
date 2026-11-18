#pragma once
#include <string>
#include <string_view>
#include <vector>
namespace voyemu {
enum class ManifestDeclaration {
    library,
    service
};
// Appends declarations without recompiling OEM resources; supports the profile UTF-16 AXML
// contract.
std::string append_manifest_declarations(std::string_view input,
                                         const std::vector<std::string> &names,
                                         ManifestDeclaration kind);
} // namespace voyemu
