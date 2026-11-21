#pragma once
// The host part of android_console.test.cpp: android_console_posix.test.cpp and
// android_console_windows.test.cpp.

// Without a free descriptor the socket of the sink fails with the owner error; Windows has no
// descriptor limit to exhaust.
void exhausted_descriptor_case();
