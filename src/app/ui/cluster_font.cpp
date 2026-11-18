#include "cluster_font.hpp"
#include "error.hpp"
#include "imgui.h"
#include "process.hpp"
#include "ui_resources.hpp"
#include <cstdio>
#include <stdexcept>

namespace voyemu::cluster {
namespace {
// Chinese labels come from the embedded Noto Sans SC subsets, each merged into the Inter face of
// its weight.
void merge_chinese(ui::Resource id) {
    const auto bytes = ui::resource(id);
    ImFontConfig config;

    config.MergeMode = true;
    config.FontDataOwnedByAtlas = false;
    static const ImWchar ranges[] = {0x3000, 0x303f, 0x4e00, 0x9fff, 0xff00, 0xffef, 0};

    if (!ImGui::GetIO().Fonts->AddFontFromMemoryTTF(const_cast<std::uint8_t *>(bytes.data()),
                                                    static_cast<int>(bytes.size()), 13, &config,
                                                    ranges)) {
        throw VoyemuError("UI_FONT", "embedded Chinese font unavailable");
    }
}

ImFont *load_face(const std::filesystem::path &path, const char *name, std::uintmax_t bytes,
                  const char *sha) {
    if (!std::filesystem::is_regular_file(path) || std::filesystem::file_size(path) != bytes ||
        sha256_file(path) != sha) {
        throw VoyemuError("UI_FONT",
                          std::string("Native ") + name + " resource does not match its exact pin");
    }

    const auto utf8 = path.u8string();
    ImFontConfig config;
    std::snprintf(config.Name, sizeof(config.Name), "%s", name);
    auto *font = ImGui::GetIO().Fonts->AddFontFromFileTTF(
            reinterpret_cast<const char *>(utf8.c_str()), 13, &config);

    if (!font) {
        throw VoyemuError("UI_FONT", "Cannot load the pinned native UI font");
    }

    for (ImWchar c = 0x20; c <= 0x7e; ++c) {
        if (!font->IsGlyphInFont(c)) {
            throw VoyemuError("UI_FONT", "Native UI font is missing a Latin UI glyph");
        }
    }

    for (ImWchar c = 0x0410; c <= 0x044f; ++c) {
        if (!font->IsGlyphInFont(c)) {
            throw VoyemuError("UI_FONT", "Native UI font is missing a Russian UI glyph");
        }
    }

    for (const ImWchar c : {0x0401, 0x0451, 0x00a9, 0x00b0, 0x00b7, 0x2014, 0x2026}) {
        if (!font->IsGlyphInFont(c)) {
            throw VoyemuError("UI_FONT", "Native UI font is missing a required UI symbol");
        }
    }

    return font;
}
} // namespace

void load_embedded_ui_font() {
    auto load = [](ui::Resource id, const char *name, const char *pin) {
        const auto bytes = ui::resource(id);

        if (sha256_bytes({reinterpret_cast<const char *>(bytes.data()), bytes.size()}) != pin) {
            throw VoyemuError("UI_FONT", "embedded bytes differ from source pin");
        }

        ImFontConfig config;
        config.FontDataOwnedByAtlas = false;
        std::snprintf(config.Name, sizeof(config.Name), "%s", name);
        auto *face = ImGui::GetIO().Fonts->AddFontFromMemoryTTF(
                const_cast<std::uint8_t *>(bytes.data()), static_cast<int>(bytes.size()), 13,
                &config);

        if (!face) {
            throw VoyemuError("UI_FONT", "embedded font unavailable");
        }

        return face;
    };
    auto *regular = load(ui::Resource::RegularFont, "Inter Regular", ui_font_sha256);

    merge_chinese(ui::Resource::ChineseFont);
    load(ui::Resource::MediumFont, "Inter Medium",
         "97ad806f526e41546d46365bb3a393145f75b7b1568913db74549ad8b8dba872");
    merge_chinese(ui::Resource::ChineseMediumFont);
    load(ui::Resource::BoldFont, "Inter Bold",
         "288316099b1e0a47a4716d159098005eef7c0066921f34e3200393dbdb01947f");
    merge_chinese(ui::Resource::ChineseBoldFont);
    ImGui::GetIO().FontDefault = regular;
}

ImFont *ui_text_font(UiFontRole role) {
    const char *name = role == UiFontRole::Emphasis ? "Inter Medium"
                       : role == UiFontRole::Choice ? "Inter Bold"
                                                    : "Inter Regular";

    for (auto *font : ImGui::GetIO().Fonts->Fonts) {
        if (std::string(font->GetDebugName()) == name) {
            return font;
        }
    }

    return ImGui::GetFont();
}

ImFont *tab_font() {
    return ui_text_font(UiFontRole::Emphasis);
}

void load_ui_font(const std::filesystem::path &path) {
    // The renderer has no variable-axis selection; use the publisher's unmodified static faces.
    auto *regular = load_face(path, "Inter Regular", 411640, ui_font_sha256);

    merge_chinese(ui::Resource::ChineseFont);
    load_face(path.parent_path() / "Inter-Medium.ttf", "Inter Medium", 417300,
              "97ad806f526e41546d46365bb3a393145f75b7b1568913db74549ad8b8dba872");
    merge_chinese(ui::Resource::ChineseMediumFont);
    load_face(path.parent_path() / "Inter-Bold.ttf", "Inter Bold", 420428,
              "288316099b1e0a47a4716d159098005eef7c0066921f34e3200393dbdb01947f");
    merge_chinese(ui::Resource::ChineseBoldFont);
    ImGui::GetIO().FontDefault = regular;
}
} // namespace voyemu::cluster
