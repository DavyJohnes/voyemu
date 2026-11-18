#include "os/locale.hpp"
#include <windows.h>

namespace voyemu::os {
std::string user_interface_language() {
    wchar_t name[LOCALE_NAME_MAX_LENGTH]{};
    const auto length = LCIDToLocaleName(MAKELCID(GetUserDefaultUILanguage(), SORT_DEFAULT), name,
                                         LOCALE_NAME_MAX_LENGTH, 0);

    if (length <= 1) {
        return {};
    }

    // Locale names are ASCII BCP 47 tags.
    std::string result;

    for (int i = 0; i + 1 < length; ++i) {
        result += static_cast<char>(name[i]);
    }

    return result;
}
} // namespace voyemu::os
