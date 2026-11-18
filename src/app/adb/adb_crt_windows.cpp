#include "sysdeps.h"

#include <cstdarg>

#undef getenv
#undef vfprintf

char *adb_getenv(const char *key) {
    return getenv(key);
}

int adb_printf(const char *format, ...) {
    va_list args;

    va_start(args, format);
    const int printed = std::vfprintf(stdout, format, args);
    va_end(args);

    return printed;
}

extern "C" int posix_strerror_r(int error, char *buffer, size_t size) {
    return strerror_s(buffer, size, error);
}
