/* Private image access shared by the e2fsprogs image helpers on every host. The image is an
 * absolute path to a regular file that is not a link and has one name; every reopen must reach the
 * same file with the same size. */
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#include <fcntl.h>
#include <io.h>
#include <stdio.h>
#else
#include <fcntl.h>
#include <limits.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

typedef struct {
    uint64_t volume, index, size;
} HelperIdentity;
typedef struct {
#ifdef _WIN32
    HANDLE handle;
#else
    int fd;
#endif
} HelperFile;

/* Keeps line ends of the parsed standard output as LF on Windows. */
static inline void helper_binary_stdout(void) {
#ifdef _WIN32
    _setmode(_fileno(stdout), _O_BINARY);
#endif
}

static inline int helper_absolute(const char *path) {
#ifdef _WIN32
    return ((path[0] >= 'A' && path[0] <= 'Z') || (path[0] >= 'a' && path[0] <= 'z')) &&
           path[1] == ':' && (path[2] == '\\' || path[2] == '/');
#else
    return path[0] == '/';
#endif
}

/* Opens path for reading and returns 1 when it is an unaliased regular file that is not a link and,
 * when expected is not null, the same file with the same size; identity receives the file. */
static inline int helper_open(const char *path, const HelperIdentity *expected, HelperFile *file,
                              HelperIdentity *identity) {
#ifdef _WIN32
    file->handle = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                               OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, NULL);
    BY_HANDLE_FILE_INFORMATION info;

    if (file->handle == INVALID_HANDLE_VALUE) {
        return 0;
    }

    if (!GetFileInformationByHandle(file->handle, &info) ||
        (info.dwFileAttributes &
         (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DEVICE)) ||
        info.nNumberOfLinks != 1) {
        CloseHandle(file->handle);

        return 0;
    }

    identity->volume = info.dwVolumeSerialNumber;
    identity->index = (uint64_t)info.nFileIndexHigh << 32 | info.nFileIndexLow;
    identity->size = (uint64_t)info.nFileSizeHigh << 32 | info.nFileSizeLow;
#else
    struct stat named, opened;

    if (lstat(path, &named) || !S_ISREG(named.st_mode) || named.st_nlink != 1) {
        return 0;
    }

    file->fd = open(path, O_RDONLY | O_NOFOLLOW);

    if (file->fd < 0) {
        return 0;
    }

    if (fstat(file->fd, &opened) || named.st_dev != opened.st_dev ||
        named.st_ino != opened.st_ino || named.st_size != opened.st_size) {
        close(file->fd);

        return 0;
    }

    identity->volume = (uint64_t)opened.st_dev;
    identity->index = (uint64_t)opened.st_ino;
    identity->size = (uint64_t)opened.st_size;
#endif

    if (expected && memcmp(expected, identity, sizeof *identity)) {
#ifdef _WIN32
        CloseHandle(file->handle);
#else
        close(file->fd);
#endif

        return 0;
    }

    return 1;
}

/* Returns 1 when the open file resolves to path itself, without links or relative components;
 * Windows compares without case and treats / as \. */
static inline int helper_canonical(HelperFile *file, const char *path) {
#ifdef _WIN32
    char resolved[MAX_PATH + 8];
    const DWORD size = GetFinalPathNameByHandleA(file->handle, resolved, sizeof resolved,
                                                 FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);

    if (!size || size >= sizeof resolved || strncmp(resolved, "\\\\?\\", 4) ||
        strlen(path) != size - 4) {
        return 0;
    }

    for (DWORD i = 0; i < size - 4; i++) {
        char a = resolved[i + 4], b = path[i] == '/' ? '\\' : path[i];

        a = a >= 'A' && a <= 'Z' ? (char)(a - 'A' + 'a') : a;
        b = b >= 'A' && b <= 'Z' ? (char)(b - 'A' + 'a') : b;

        if (a != b) {
            return 0;
        }
    }

    return 1;
#else
    char resolved[PATH_MAX];

    (void)file;

    return realpath(path, resolved) && !strcmp(resolved, path);
#endif
}

/* Reads exactly size bytes at offset; returns 1 on success. */
static inline int helper_read(HelperFile *file, uint64_t offset, void *buffer, size_t size) {
#ifdef _WIN32
    unsigned char *out = buffer;

    while (size) {
        OVERLAPPED at = {0};
        DWORD chunk = size > (1U << 30) ? 1U << 30 : (DWORD)size, done = 0;

        at.Offset = (DWORD)offset;
        at.OffsetHigh = (DWORD)(offset >> 32);

        if (!ReadFile(file->handle, out, chunk, &done, &at) || done != chunk) {
            return 0;
        }

        out += chunk;
        offset += chunk;
        size -= chunk;
    }

    return 1;
#else
    return offset <= INT64_MAX && pread(file->fd, buffer, size, (off_t)offset) == (ssize_t)size;
#endif
}

static inline void helper_close(HelperFile *file) {
#ifdef _WIN32
    CloseHandle(file->handle);
#else
    close(file->fd);
#endif
}
