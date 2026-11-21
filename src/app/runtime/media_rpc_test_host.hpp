#pragma once
// The host parts of media_rpc.test.cpp: media_rpc_posix.test.cpp and media_rpc_windows.test.cpp.
#include <cstddef>
#include <filesystem>

// Creates the empty SDK descriptor at path with the private security of Voyemu state on Windows;
// POSIX writes it with the ordinary file stream.
void create_private_descriptor(const std::filesystem::path &path);
// Makes Everyone the owner of path; true when the SID read back confirms it. Windows only.
bool assign_world_owner(const std::filesystem::path &path);
// Whether the mapped frame file has bytes and is private to the current user.
bool private_frame_file(const std::filesystem::path &path, std::size_t bytes);
