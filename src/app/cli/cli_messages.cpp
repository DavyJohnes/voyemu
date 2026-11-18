#include "cli_messages.hpp"
#include "model.hpp"
#include <iterator>
namespace voyemu::cli {
namespace {
#include "cli_messages.inc"

const Json &catalog(Language language) {
    static const std::array<Json, 3> catalogs{
            Json::parse(std::begin(embedded_cli_messages_en), std::end(embedded_cli_messages_en)),
            Json::parse(std::begin(embedded_cli_messages_ru), std::end(embedded_cli_messages_ru)),
            Json::parse(std::begin(embedded_cli_messages_zh), std::end(embedded_cli_messages_zh))};

    return catalogs.at(static_cast<std::size_t>(language));
}
} // namespace

std::string_view message_catalog(Language language) {
    const auto bytes = [](const auto &array) {
        return std::string_view(reinterpret_cast<const char *>(array), sizeof(array));
    };

    switch (language) {
    case Language::russian:
        return bytes(embedded_cli_messages_ru);
    case Language::chinese:
        return bytes(embedded_cli_messages_zh);
    case Language::english:
        break;
    }

    return bytes(embedded_cli_messages_en);
}

std::string message(Language language, std::string_view key, const MessageArguments &arguments) {
    const auto &texts = catalog(language);
    const auto found = texts.find(key);

    if (found == texts.end() || !found->is_string()) {
        throw std::logic_error("CLI_TEXT: missing message " + std::string(key));
    }

    const auto &text = found->get_ref<const std::string &>();
    std::string result;

    for (std::size_t i = 0; i < text.size();) {
        if (text[i] != '{') {
            result += text[i++];
            continue;
        }

        const auto end = text.find('}', i);
        const auto argument =
                end == std::string::npos
                        ? arguments.end()
                        : arguments.find(std::string_view(text).substr(i + 1, end - i - 1));

        if (argument == arguments.end()) {
            throw std::logic_error("CLI_TEXT: missing argument in " + std::string(key));
        }

        result += argument->second;
        i = end + 1;
    }

    return result;
}

std::optional<Language> language_option(std::string_view value) {
    for (const auto language : languages) {
        if (value == ui::language_tag(language)) {
            return language;
        }
    }

    return std::nullopt;
}

Language resolve_language(std::optional<std::string_view> option,
                          const EnvironmentLookup &environment,
                          const InterfaceLanguage &interface_language) {
    if (option) {
        if (const auto selected = language_option(*option)) {
            return *selected;
        }
    }

    for (const auto *name : {"LC_ALL", "LC_MESSAGES", "LANG"}) {
        if (const auto *value = environment(name); value && *value) {
            return ui::language_from_tag(value);
        }
    }

    const auto host = interface_language();

    return host.empty() ? Language::english : ui::language_from_tag(host);
}

Language select_language(int argc, char **argv, const EnvironmentLookup &environment,
                         const InterfaceLanguage &interface_language) {
    std::optional<std::string_view> option;

    for (int i = 1; i < argc && !option; ++i) {
        const std::string_view argument = argv[i];

        if (argument.starts_with("--lang=")) {
            option = argument.substr(7);
        }
    }

    return resolve_language(option, environment, interface_language);
}

LocalizedError::LocalizedError(std::string code, std::string key, MessageArguments arguments)
    : VoyemuError(code.c_str(), message(Language::english, key, arguments)), code_(std::move(code)),
      key_(std::move(key)), arguments_(std::move(arguments)) {}

std::string LocalizedError::render(Language language) const {
    return code_ + ": " + message(language, key_, arguments_);
}
} // namespace voyemu::cli
