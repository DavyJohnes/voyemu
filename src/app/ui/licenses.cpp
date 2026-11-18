#include "licenses.hpp"
namespace voyemu::ui {
namespace {
#include "licenses_data.inc"
}

std::span<const LicenseNotice> embedded_license_notices() {
    return license_entries;
}

std::string license_document(const LicenseNotice &notice) {
    return std::string(notice.text);
}

std::string_view license_site(const LicenseNotice &notice, Language language) {
    return notice.component == "Voyemu" && language == Language::russian ? "https://voyemu.ru"
                                                                         : notice.url;
}
} // namespace voyemu::ui
