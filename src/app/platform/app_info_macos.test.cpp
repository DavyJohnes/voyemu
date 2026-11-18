#include <CoreFoundation/CoreFoundation.h>
#include <cstdio>
#include <string>

// The embedded Info.plist gives the unbundled executable the Voyemu bundle identity.
int main() {
    const auto identifier = CFBundleGetIdentifier(CFBundleGetMainBundle());
    char text[64] = {};

    if (identifier == nullptr ||
        !CFStringGetCString(identifier, text, sizeof text, kCFStringEncodingUTF8) ||
        std::string(text) != "com.voyemu") {
        std::fprintf(stderr, "bundle identifier: %s\n", text);

        return 1;
    }

    return 0;
}
