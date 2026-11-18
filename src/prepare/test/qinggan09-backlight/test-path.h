/* glibc hides POSIX declarations under -std=c11; bionic and macOS declare them anyway. */
#define _DEFAULT_SOURCE
#include <stdlib.h>
#define NVRAM_FILE getenv("VOYEMU_TEST_NVRAM")
#define NVRAM_SEED getenv("VOYEMU_TEST_NVRAM_SEED")
