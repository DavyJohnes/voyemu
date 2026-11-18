#pragma once
#include <string>
#include <string_view>
namespace voyemu {
std::string adapt_qnx_boot_script(std::string_view original);
std::string adapt_qnx_procnto_environment(std::string_view original);
std::string adapt_qnx_qinggan11_boot_script(std::string_view original);
std::string adapt_qnx_qinggan11_procnto_environment(std::string_view original);
std::string adapt_qnx_qinggan11_symlinks(std::string_view original);
std::string adapt_qnx_symlinks(std::string_view original);
std::string adapt_qnx_passion_symlinks(std::string_view original);
std::string adapt_qnx_qinggan11_startupmgr_gpt(std::string_view original);
std::string adapt_qnx_qinggan11_startupmgr_dispatch(std::string_view original,
                                                    std::string_view readstub);
std::string qnx_qinggan11_developer_candidate(std::string_view original, std::string_view readstub,
                                              std::string_view display_sha256,
                                              std::string_view disk_sha256);
std::string adapt_qnx_startupmgr(std::string_view original, std::string_view readstub,
                                 std::string_view display_sha256, std::string_view disk_sha256);
} // namespace voyemu
