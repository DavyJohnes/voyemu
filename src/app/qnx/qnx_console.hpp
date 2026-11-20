#pragma once
#include "qnx_adapter.hpp"
#include <string>
#include <string_view>
namespace voyemu {
// Exact A11-family layout guards; this helper never mutates OEM inputs.
void qnx_a11_console_layout_guard(std::string_view original);
// Opt-in after source assembly for the qinggan11 and Passion adapters; original is the pinned host
// image of the adapter. The caller supplies our pinned LLVM-built blob.
std::string adapt_qnx_pl011_console(QnxGuestAdapter adapter, std::string_view original,
                                    std::string_view original_sha256,
                                    std::string_view prepared_kernel, std::string_view own_console);
} // namespace voyemu
