#pragma once
#include <string>
#include <string_view>
namespace voyemu {
// Returns the bytes preparation installs for a QNX guest shell script: comments, leading
// indentation, trailing blanks and blank lines go, every other byte and every line break stays.
// Quoted text, parameter expansions and the #! line are kept. Here-documents, $'...' quoting and
// comments inside command substitutions are refused, as is a script without a final LF or with an
// unterminated quote or substitution.
std::string prepare_qnx_guest_shell(std::string_view script);
} // namespace voyemu
