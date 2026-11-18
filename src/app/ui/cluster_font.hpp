#pragma once
#include <filesystem>

struct ImFont;
namespace voyemu::cluster {
inline constexpr const char *ui_font_sha256 =
        "40d692fce188e4471e2b3cba937be967878f631ad3ebbbdcd587687c7ebe0c82";
enum class UiFontRole {
    Body,
    Label,
    Emphasis,
    // Captions of the radio choices: scale, language and gear.
    Choice
};
ImFont *ui_text_font(UiFontRole role);
ImFont *tab_font();
void load_ui_font(const std::filesystem::path &path);
void load_embedded_ui_font();
} // namespace voyemu::cluster
