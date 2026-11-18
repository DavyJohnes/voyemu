#include "ui_resources.hpp"
#include <stdexcept>
namespace voyemu::ui {
namespace {
#include "ui_resources.inc"
}

std::span<const std::uint8_t> resource(Resource id) {
    switch (id) {
    case Resource::RegularFont:
        return regular_font;
    case Resource::MediumFont:
        return medium_font;
    case Resource::BoldFont:
        return bold_font;
    case Resource::Icon:
        return application_icon;
    case Resource::LogoText:
        return logo_text;
    case Resource::LogoFigure:
        return logo_figure;
    case Resource::ScreensIcon:
        return screens_icon;
    case Resource::ChineseFont:
        return chinese_font;
    case Resource::ChineseMediumFont:
        return chinese_medium_font;
    case Resource::ChineseBoldFont:
        return chinese_bold_font;
    }

    throw std::invalid_argument("UI_RESOURCE: unknown resource");
}
} // namespace voyemu::ui
