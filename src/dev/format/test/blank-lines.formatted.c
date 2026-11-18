/* Formatter fixture: blank lines between C definitions. */
#include <sys/stat.h>

static const char *const names[] = {"a", "b"};
static int counter;
static int helper(int value);
static int other(int value);

static int helper(int value) {
    struct stat info;

    info.st_size = value;

    return (int)info.st_size;
}

/* Doubles its argument. */
static int other(int value) {
    return value * 2;
}

int total(void) {
    return helper(counter) + other(counter) + (int)sizeof(names);
}
