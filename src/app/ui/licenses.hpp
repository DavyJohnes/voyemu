#pragma once
#include "ui_localization.hpp"
#include <span>
#include <string>
#include <string_view>
namespace voyemu::ui {
struct LicenseNotice {
    std::string_view component, authors, license, url, text;
};
std::span<const LicenseNotice> embedded_license_notices();
std::string license_document(const LicenseNotice &notice);
// Voyemu's own site follows the UI language; other components keep their upstream site.
std::string_view license_site(const LicenseNotice &notice, Language language);
} // namespace voyemu::ui
