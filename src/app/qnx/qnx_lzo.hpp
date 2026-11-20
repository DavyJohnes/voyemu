#pragma once
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
// Exact owner image layout of the qinggan11 adapter; does not select or apply startup patches.
std::string decode_qnx_qinggan11_host(std::string_view hyp, std::string_view hyp_sha256);
// Exact owner stream of the older qinggan11 adapter, independently pinned.
std::string decode_qnx_qinggan11_older_host(std::string_view hyp, std::string_view hyp_sha256);
std::string encode_qnx_qinggan11_older_host(std::string_view hyp, std::string_view hyp_sha256,
                                            std::string_view plaintext);
// A11 has 103 blocks; roundtrip validity does not imply a verified guest landing.
std::string encode_qnx_qinggan11_host(std::string_view hyp, std::string_view hyp_sha256,
                                      std::string_view plaintext);
// Re-packs changed plaintext with the vendored LZO1X codec and verifies the full roundtrip.
std::string encode_qnx_boot_stream(std::string_view reference, std::string_view plaintext);
} // namespace voyemu
