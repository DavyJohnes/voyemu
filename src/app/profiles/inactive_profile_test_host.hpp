#pragma once
// The host parts of inactive_profile.test.cpp: inactive_profile_posix.test.cpp and
// inactive_profile_windows.test.cpp, each with the lease holders of its host.
#include <stdexcept>

inline void check(bool yes, const char *why) {
    if (!yes) {
        throw std::runtime_error(why);
    }
}

// Runs the inactive profile lease cases or the holder child that argv selects; the exit code.
int inactive_profile_case(int argc, char **argv);
