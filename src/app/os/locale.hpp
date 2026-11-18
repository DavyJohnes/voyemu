#pragma once
#include <string>

namespace voyemu::os {
// The user's UI language as a locale name such as "ru-RU"; empty when the host has no such
// setting beyond the locale environment variables.
std::string user_interface_language();
} // namespace voyemu::os
