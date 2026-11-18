#pragma once
#include <cstdint>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
namespace voyemu {
struct QnxElfSection {
    std::string name;
    std::uint64_t type = 0, flags = 0, address = 0, offset = 0, size = 0, link = 0;
};
// Section headers of a little-endian ELF64 AArch64 file, bounds-checked against the file.
std::vector<QnxElfSection> qnx_elf_sections(std::string_view elf);
// Names of the defined dynamic symbols an OEM library exports under expected_soname.
std::set<std::string> qnx_link_exports(std::string_view elf, std::string_view expected_soname);
struct QnxLinkImports {
    std::vector<std::string> needed;
    std::set<std::string> strong, weak;
};
// DT_NEEDED entries and undefined dynamic symbols of a guest module.
QnxLinkImports qnx_link_imports(std::string_view elf);
// Guest modules link with their imports left undefined; this checks, as --no-undefined against the
// OEM libraries did, that the module needs exactly these sonames in order and that one of these
// OEM libraries exports every strong import.
void verify_qnx_link_imports(
        std::string_view module,
        const std::vector<std::pair<std::string, std::string_view>> &libraries);
// Checks that these libraries together export every strong import of the module, in any order.
void verify_link_exports(std::string_view module,
                         const std::vector<std::pair<std::string, std::string_view>> &libraries);
} // namespace voyemu
