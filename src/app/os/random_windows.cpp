#include "error.hpp"
#include "os/random.hpp"
#include <windows.h>
#include <bcrypt.h>
namespace voyemu::os {
void random_bytes(unsigned char *data, std::size_t size) {
    if (BCryptGenRandom(nullptr, data, static_cast<ULONG>(size), BCRYPT_USE_SYSTEM_PREFERRED_RNG) !=
        0) {
        throw VoyemuError("RANDOM", "system RNG failed");
    }
}
} // namespace voyemu::os
