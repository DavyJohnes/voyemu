#include "os/locale.hpp"

namespace voyemu::os {
// Linux carries the language only in LC_ALL, LC_MESSAGES and LANG.
std::string user_interface_language() {
    return {};
}
} // namespace voyemu::os
