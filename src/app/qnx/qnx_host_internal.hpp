#pragma once
#include "model.hpp"
namespace voyemu {
// Strict parser for a host binding section: the Windows section passes the same schema,
// engine/ANGLE role and mechanics checks the POSIX factory form applies in its own terms.
bool qnx_host_managed_windows_bindings(const Json &value);

// One consistent read of the shared host binding file: its identity, bytes and their SHA-256
// stayed the same while it was read.
struct QnxHostBindingFile {
    Json identity;
    std::string bytes;
    std::string pin;
    Json document;
};

QnxHostBindingFile read_qnx_host_binding_file(const std::filesystem::path &file);
} // namespace voyemu
