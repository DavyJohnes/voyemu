#pragma once
#include <string>
#include <string_view>
namespace voyemu {
// Pure collision guard, exposed to verify package/certificate precedence on synthetic inputs.
bool original_navi_mac_collision(std::string_view xml, std::string_view certificate);
// Inserts the platform signer mapping for certificate before the single policy terminator;
// rejects collisions and preserves every original byte. Pin-free, for synthetic certificates.
std::string original_navi_mac_insert(std::string_view xml, std::string_view certificate);
// Exact A11/API30 input and verified OEM DER only; preserves every original byte.
std::string apply_original_navi_mac(std::string_view profile, unsigned api, std::string_view xml,
                                    std::string_view certificate);
} // namespace voyemu
