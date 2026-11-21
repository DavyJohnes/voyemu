#include "android_adb_test_host.hpp"
#include "os/private_json_reader.hpp"
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdio>
#include <fcntl.h>
#include <io.h>

std::string read_probe_output(const std::filesystem::path &path) {
    voyemu::detail::PrivateJsonReader file(path, 4096);

    return file.text();
}

bool await_probe_child(std::int64_t pid) {
    const HANDLE child = OpenProcess(SYNCHRONIZE, FALSE, static_cast<DWORD>(pid));

    if (!child) {
        return false;
    }

    const auto terminal = WaitForSingleObject(child, 5000);
    CloseHandle(child);

    return terminal == WAIT_OBJECT_0;
}

void binary_standard_output() {
    _setmode(_fileno(stdout), _O_BINARY);
}
