#include "version.hpp"
namespace voyemu {
std::string_view version() {
    return VOYEMU_VERSION;
}

std::string_view release_date() {
    return VOYEMU_RELEASE_DATE;
}
} // namespace voyemu
