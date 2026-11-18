#pragma once
#include <string_view>
namespace voyemu {
// The launcher version, VOYEMU_VERSION of src/app/CMakeLists.txt, such as 1.0.0 or 1.0.0-beta1.
std::string_view version();
// The release date YYYY-MM-DD, VOYEMU_RELEASE_DATE of src/app/CMakeLists.txt.
std::string_view release_date();
} // namespace voyemu
