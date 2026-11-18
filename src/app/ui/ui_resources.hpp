#pragma once
#include <cstdint>
#include <span>
namespace voyemu::ui {
enum class Resource {
    RegularFont,
    MediumFont,
    BoldFont,
    Icon,
    LogoText,
    LogoFigure,
    ScreensIcon,
    ChineseFont,
    ChineseMediumFont,
    ChineseBoldFont
};
std::span<const std::uint8_t> resource(Resource id);
} // namespace voyemu::ui
