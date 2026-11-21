#pragma once
// Synthetic owned process used by the adjacent monitor and Session lifecycle tests; no VM. The
// POSIX fixture serves Unix sockets (shutdown_fixture_posix.cpp), the Windows fixture loopback TCP
// (shutdown_fixture_windows.cpp).

namespace voyemu::shutdown_fixture {
// Runs the fixture mode of argv; the exit code of the fixture process.
int run(int argc, char **argv);
} // namespace voyemu::shutdown_fixture
