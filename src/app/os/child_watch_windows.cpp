#include "os/child_watch.hpp"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <tlhelp32.h>

namespace voyemu::os {
ChildWatch::~ChildWatch() {
    if (handle_) {
        CloseHandle(reinterpret_cast<HANDLE>(handle_));
    }
}

ChildWatch::Opening ChildWatch::open(std::uint64_t pid) {
    if (pid == 0 || pid > MAXDWORD) {
        return Opening::invalid;
    }

    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE,
                                 static_cast<DWORD>(pid));

    if (!process || WaitForSingleObject(process, 0) != WAIT_TIMEOUT) {
        if (process) {
            CloseHandle(process);
        }

        return Opening::unavailable;
    }

    bool child = pid == GetCurrentProcessId();
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);

    if (snapshot != INVALID_HANDLE_VALUE) {
        PROCESSENTRY32 row{};
        row.dwSize = sizeof row;

        if (Process32First(snapshot, &row)) {
            do {
                if (row.th32ProcessID == pid && row.th32ParentProcessID == GetCurrentProcessId()) {
                    child = true;
                }
            } while (Process32Next(snapshot, &row));
        }

        CloseHandle(snapshot);
    }

    if (!child) {
        CloseHandle(process);

        return Opening::foreign;
    }

    if (handle_) {
        CloseHandle(reinterpret_cast<HANDLE>(handle_));
    }

    handle_ = reinterpret_cast<std::intptr_t>(process);
    pid_ = pid;

    return Opening::opened;
}

bool ChildWatch::alive() const {
    return handle_ && WaitForSingleObject(reinterpret_cast<HANDLE>(handle_), 0) == WAIT_TIMEOUT;
}
} // namespace voyemu::os
