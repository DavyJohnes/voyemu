#pragma once
// The host parts of qnx_process_ram.test.cpp: qnx_process_ram_posix.test.cpp and
// qnx_process_ram_windows.test.cpp.

// The persistent storage of the native diagnostic engine over owned sparse disks; Windows only.
void diagnostic_persistent_storage_case();
// Retained disk handles, replacement and extent guards and the durable flush of persistent storage
// on the files of this host.
void persistent_storage_case();
