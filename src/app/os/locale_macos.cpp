#include "os/locale.hpp"
#include <CoreFoundation/CoreFoundation.h>

namespace voyemu::os {
// The first preferred language of the user, such as "ru-RU"; applications started from Finder
// carry no locale environment variables.
std::string user_interface_language() {
    const CFArrayRef languages = CFLocaleCopyPreferredLanguages();

    if (languages == nullptr) {
        return {};
    }

    std::string result;

    if (CFArrayGetCount(languages) > 0) {
        // Language identifiers are ASCII BCP 47 tags.
        char name[64]{};

        if (CFStringGetCString(static_cast<CFStringRef>(CFArrayGetValueAtIndex(languages, 0)), name,
                               sizeof name, kCFStringEncodingASCII)) {
            result = name;
        }
    }

    CFRelease(languages);

    return result;
}
} // namespace voyemu::os
