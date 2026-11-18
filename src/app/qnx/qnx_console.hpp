#pragma once
#include <string>
#include <string_view>
namespace voyemu {
// Exact A11-family layout guards; this helper never mutates OEM inputs.
void qnx_a11_console_layout_guard(std::string_view original);
// A11-only opt-in after source assembly. The caller supplies our pinned LLVM-built blob.
std::string adapt_qnx_qinggan11_611_pl011_console(std::string_view original,
                                                  std::string_view original_sha256,
                                                  std::string_view prepared_kernel,
                                                  std::string_view own_console);
std::string adapt_qnx_a11_pl011_console(std::string_view original, std::string_view original_sha256,
                                        std::string_view prepared_kernel,
                                        std::string_view own_console);
} // namespace voyemu
