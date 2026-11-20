#pragma once
#include "qnx_adapter.hpp"
#include <string>
#include <string_view>
namespace voyemu {
// Strict explicitly declared stream geometry for source-pinned hardware adapters.
std::string decode_qnx_declared_boot_stream(std::string_view stream, unsigned blocks,
                                            std::size_t bytes);
std::string encode_qnx_declared_boot_stream(std::string_view reference, std::string_view plaintext,
                                            unsigned blocks, std::size_t bytes);
// Decodes the OEM boot stream with the vendored LZO1X codec.
std::string decode_qnx_boot_stream(std::string_view stream);
// The 103-block host stream of a qinggan11 adapter at the exact image layout of its registry entry;
// does not select or apply startup patches. Roundtrip validity does not imply a verified guest
// landing.
std::string decode_qnx_qinggan11_host(QnxGuestAdapter adapter, std::string_view hyp,
                                      std::string_view hyp_sha256);
std::string encode_qnx_qinggan11_host(QnxGuestAdapter adapter, std::string_view hyp,
                                      std::string_view hyp_sha256, std::string_view plaintext);
// Re-packs changed plaintext with the vendored LZO1X codec and verifies the full roundtrip.
std::string encode_qnx_boot_stream(std::string_view reference, std::string_view plaintext);
} // namespace voyemu
