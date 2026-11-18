#include "cluster_font.hpp"
#include "imgui.h"
#include "imgui_internal.h"
#include "parameters_layout.hpp"
#include "ui_localization.hpp"
#include <array>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>

using namespace voyemu::cluster;
namespace {
void check(bool condition, const char *message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void font_guards(const std::filesystem::path &path) {
    ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;
    load_embedded_ui_font();
    check(ImGui::GetIO().Fonts->Fonts.Size == 3,
          "embedded fonts load without paths or sibling resource files");
    ImGui::DestroyContext();
    ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;
    try {
        bool rejected = false;
        try {
            load_ui_font(path.string() + ".missing");
        } catch (...) {
            rejected = true;
        }

        check(rejected, "missing native resource cannot silently fall back to an unreadable font");
        std::ifstream input(path, std::ios::binary);
        std::string bytes((std::istreambuf_iterator<char>(input)), {});
        check(!bytes.empty() && bytes.size() < 512 * 1024,
              "font fixture has a finite static resource bound");
        bytes[0] ^= 1;
        const auto scratch = path.parent_path().parent_path().parent_path().parent_path() /
                             ".local/build/tmp";
        std::filesystem::create_directories(scratch);
        const auto temporary =
                scratch /
                (".voyemu-font-" +
                 std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) +
                 ".ttf");
        {
            std::ofstream output(temporary, std::ios::binary);
            output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
            check(bool(output), "temporary tampered fixture can be written");
        }
        rejected = false;
        try {
            load_ui_font(temporary);
        } catch (...) {
            rejected = true;
        }

        std::filesystem::remove(temporary);
        check(rejected, "changed default Regular resource cannot bypass its independent SHA guard");
        load_ui_font(path);
        auto &io = ImGui::GetIO();
        io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
        io.DisplaySize = ImVec2(1294, 360);
        io.DeltaTime = 1.f / 60;
        ImGui::GetStyle().FontSizeBase = 13;
        check(std::string(io.FontDefault->GetDebugName()) == "Inter Regular",
              "official Regular remains the default field font");
        ImGui::NewFrame();
        ImGui::Begin("font metric guards");
        check(io.Fonts->Fonts.Size == 3, "native shells load exactly three bounded Inter faces");
        check(std::string(tab_font()->GetDebugName()) == "Inter Medium",
              "tabs use Medium500 without synthetic or Bold700 inflation");
        check(ui_text_font(UiFontRole::Body) == io.FontDefault,
              "body role is the default official Regular face");
        check(ui_text_font(UiFontRole::Label) == io.FontDefault,
              "labels and body share the same Regular face");
        check(ui_text_font(UiFontRole::Emphasis) == tab_font() && tab_font() != io.FontDefault,
              "tabs and group headings share Medium500 above Regular400 body");
        // Chinese captions sit beside Latin ones with a comparable stroke: the ink of 中 and 文 is
        // close to that of E and N in Regular, and heavier in Medium.
        const auto ink = [&](ImFont *font, const char *text) {
            float total = 0;

            while (*text) {
                unsigned int codepoint = 0;
                text += ImTextCharFromUtf8(&codepoint, text, nullptr);
                const auto *glyph =
                        font->GetFontBaked(26)->FindGlyph(static_cast<ImWchar>(codepoint));
                auto *atlas = io.Fonts->TexData;
                check(glyph && glyph->Visible && glyph->Codepoint == codepoint,
                      "caption glyph is rasterized");

                for (int y = std::lround(glyph->V0 * atlas->Height);
                     y < std::lround(glyph->V1 * atlas->Height); ++y) {
                    for (int x = std::lround(glyph->U0 * atlas->Width);
                         x < std::lround(glyph->U1 * atlas->Width); ++x) {
                        const auto *pixel =
                                static_cast<const unsigned char *>(atlas->GetPixelsAt(x, y));
                        total += atlas->Format == ImTextureFormat_Alpha8 ? pixel[0] : pixel[3];
                    }
                }
            }

            return total / 255;
        };
        const float latin = ink(io.FontDefault, "EN"), chinese = ink(io.FontDefault, "中文");
        check(chinese > .8f * latin && chinese < 1.4f * latin,
              "Regular Chinese captions have the stroke weight of Regular Latin captions");
        check(ink(tab_font(), "中文") > 1.1f * chinese,
              "Medium Chinese captions are heavier than Regular ones");
        check(std::string(ui_text_font(UiFontRole::Choice)->GetDebugName()) == "Inter Bold" &&
                      ink(ui_text_font(UiFontRole::Choice), "EN") > 1.15f * latin &&
                      ink(ui_text_font(UiFontRole::Choice), "中文") > ink(tab_font(), "中文"),
              "choice captions use Inter Bold with heavier Chinese glyphs than Medium");
#ifdef __APPLE__
        for (const ImWchar c : {0x901f, 0x5ea6, 0x8f66, 0x8eab, 0x706f, 0x5149, 0x8f6e, 0x80ce}) {
            check(io.FontDefault->IsGlyphInFont(c) &&
                          ui_text_font(UiFontRole::Label)->IsGlyphInFont(c) &&
                          ui_text_font(UiFontRole::Emphasis)->IsGlyphInFont(c),
                  "system fallback covers Chinese UI in both faces");
        }

        for (const auto &entry : voyemu::ui::catalogue) {
            const char *text = entry.chinese;

            while (*text) {
                unsigned int codepoint = 0;
                const int bytes = ImTextCharFromUtf8(&codepoint, text, nullptr);
                check(bytes > 0, "Chinese catalogue is valid UTF-8");
                text += bytes;
                check(io.FontDefault->IsGlyphInFont(static_cast<ImWchar>(codepoint)) &&
                              ui_text_font(UiFontRole::Label)
                                      ->IsGlyphInFont(static_cast<ImWchar>(codepoint)) &&
                              ui_text_font(UiFontRole::Emphasis)
                                      ->IsGlyphInFont(static_cast<ImWchar>(codepoint)),
                      "every Chinese catalogue glyph exists in both UI faces");
            }
        }
#endif
        check(ImGui::CalcTextSize("4294967295").x <= ParametersLayout::raw_value_width - 20,
              "all ten digits of a raw uint32 maximum fit inside the numeric field padding");

        for (const auto language : {voyemu::ui::Language::russian, voyemu::ui::Language::english,
                                    voyemu::ui::Language::chinese}) {
            voyemu::ui::current_language = language;

            for (const unsigned scale : {canonical_ui_scale, 75u, 100u}) {
                const auto layout = parameters_layout(1294, 360, 0, scale);
                const std::array<const char *, 5> captions = {"Движение", "Кузов", "Свет", "Шины",
                                                              "Все"};
                ImGui::PushFont(tab_font(), layout.tab_font_size());
                std::array<float, 5> widths{};

                for (std::size_t i = 0; i < captions.size(); ++i) {
                    widths[i] = ImGui::CalcTextSize(voyemu::ui::tr(captions[i])).x;
                }

                const auto tabs = parameter_tabs(layout, widths, true);
                check(tabs.font_scale > 0 && tabs.font_scale <= 1,
                      "localized navigation has a finite bounded font scale");

                if (layout.compact50()) {
                    check(tabs.font_scale == 1,
                          "50percent navigation preserves the measured reference size without locale shrinking");
                }

                for (const auto &tab : tabs.items) {
                    check(tab.x >= layout.body.x && tab.right() <= layout.body.right() + .001f,
                          "localized navigation fits the available row");
                }

                ImGui::PopFont();

                for (const auto *label :
                     {"Передняя левая", "Передняя правая", "Задняя левая", "Задняя правая",
                      "Габариты", "Ближний", "Дальний", "Дневные огни", "Туман сзади"}) {
                    const auto extent = ui_text_font(UiFontRole::Label)
                                                ->CalcTextSizeA(layout.label_font_size(), FLT_MAX,
                                                                0, voyemu::ui::tr(label));
                    check(extent.x <= layout.body.width - 49 &&
                                  extent.y == layout.label_font_size(),
                          "full production-font door and lamp captions fit one enlarged line");
                }

                for (const auto *label : {"Капот", "Багажник", "Левый", "Правый"}) {
                    const auto extent = ui_text_font(UiFontRole::Label)
                                                ->CalcTextSizeA(layout.label_font_size(), FLT_MAX,
                                                                0, voyemu::ui::tr(label));
                    check(extent.x <= layout.column(2, 0).width - 49,
                          "short paired captions retain the full enlarged font size");
                }
            }
        }

        ImGui::End();
        ImGui::Render();
        ImGui::DestroyContext();
    } catch (...) {
        ImGui::DestroyContext();
        throw;
    }
}
} // namespace

int main(int argc, char **argv) {
    try {
        if (argc != 2) {
            throw std::runtime_error("font test requires the approved static font path");
        }

        font_guards(argv[1]);
        std::cout << "native font guards: pass\n";

        return 0;
    } catch (const std::exception &error) {
        std::cerr << "native font guards: " << error.what() << '\n';

        return 1;
    }
}
