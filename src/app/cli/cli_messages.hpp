#pragma once
#include "error.hpp"
#include "ui_localization.hpp"
#include <array>
#include <functional>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
namespace voyemu::cli {
using Language = ui::Language;
using MessageArguments = std::map<std::string, std::string, std::less<>>;
// Catalog languages; English is the source of keys and of every error what().
inline constexpr std::array languages{Language::english, Language::russian, Language::chinese};
// Embedded cli/messages.<tag>.json of one language.
std::string_view message_catalog(Language language);
// Replaces each {name} with its argument; a missing key or argument is a programming error.
std::string message(Language language, std::string_view key,
                    const MessageArguments &arguments = {});
// The exact --lang value: en, ru or zh.
std::optional<Language> language_option(std::string_view value);
using EnvironmentLookup = std::function<const char *(const char *)>;
using InterfaceLanguage = std::function<std::string()>;
// First match wins: a valid --lang, then the first non-empty LC_ALL, LC_MESSAGES or LANG, then the
// host UI language, then English. An unknown or C/POSIX locale means English.
Language resolve_language(std::optional<std::string_view> option,
                          const EnvironmentLookup &environment,
                          const InterfaceLanguage &interface_language);
// Resolves the language of a command line from its first --lang=VALUE.
Language select_language(int argc, char **argv, const EnvironmentLookup &environment,
                         const InterfaceLanguage &interface_language);
// A user-facing CLI error: what() stays "CODE: English message" for JSON and logs, while human
// output renders the same key in the selected language.
class LocalizedError : public VoyemuError {
public:
    LocalizedError(std::string code, std::string key, MessageArguments arguments = {});
    [[nodiscard]] std::string render(Language language) const;

private:
    std::string code_, key_;
    MessageArguments arguments_;
};
} // namespace voyemu::cli
