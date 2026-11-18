#include "ui_localization.hpp"
#include <iostream>
#include <set>
#include <stdexcept>
using namespace voyemu::ui;

void require(bool condition) {
    if (!condition) {
        throw std::runtime_error("UI localization invariant");
    }
}

int main() try {
    require(language_from_tag("zh-Hans-CN") == Language::chinese);
    require(language_from_tag("zh_TW") == Language::chinese);
    require(language_from_tag("ru-RU") == Language::russian);
    require(language_from_tag("ru_US") == Language::russian);
    require(language_from_tag("en-RU") == Language::english);
    require(language_from_tag("de-DE") == Language::english);
    require(language_from_tag("") == Language::english);
    std::set<std::string_view> keys;

    for (const auto &entry : catalogue) {
        require(keys.insert(entry.russian).second);
        require(!std::string_view(entry.english).empty());
        require(!std::string_view(entry.chinese).empty());
        current_language = Language::chinese;
        require(std::string_view(tr(entry.russian)) == entry.chinese);
        current_language = Language::english;
        require(std::string_view(tr(entry.russian)) == entry.english);
        current_language = Language::russian;
        require(std::string_view(tr(entry.russian)) == entry.russian);
    }

    current_language = Language::russian;
    require(release_text("2026-11-08") == "Выпущена 8 ноября 2026");
    require(release_text("2027-01-31") == "Выпущена 31 января 2027");
    current_language = Language::chinese;
    require(release_text("2026-11-08") == "发布于2026年11月8日");
    current_language = Language::english;
    require(release_text("2026-11-08") == "Released 8 November 2026");
    require(release_text("2027-12-01") == "Released 1 December 2027");
    require(std::string_view(tr("О программе")) == "About");
    require(std::string_view(tr("Сайт")) == "Website");
    require(std::string_view(tr("Завершить Voyemu")) == "Quit Voyemu");
    require(std::string_view(tr("Давление")) == "Pressure");
    require(std::string_view(tr("Температура")) == "Temperature");
    require(std::string_view(tr("Все")) == "All");
    require(std::string_view(tr("Двери")) == "Doors");
    require(std::string_view(tr("Левые")) == "Left");
    require(std::string_view(tr("Правые")) == "Right");
    require(std::string_view(tr("Передняя")) == "Front");
    require(std::string_view(tr("Задняя")) == "Rear");
    require(std::string_view(tr("Аварийка")) == "Hazard lights");
    require(std::string_view(tr("ЛП")) == "FL");
    require(std::string_view(tr("ПП")) == "FR");
    require(std::string_view(tr("ЛЗ")) == "RL");
    require(std::string_view(tr("ПЗ")) == "RR");
    require(std::string_view(tr("Поворотник")) == "Indicator");
    require(std::string_view(tr("Левый")) == "Left");
    require(std::string_view(tr("Правый")) == "Right");
    require(std::string_view(tr("Передняя левая")) == "Front left");
    require(std::string_view(tr("bcm_tyreLFPressure")) == "bcm_tyreLFPressure");
    require(std::string_view(language_tag(Language::russian)) == "ru");
    require(std::string_view(language_tag(Language::chinese)) == "zh");
    current_language = Language::chinese;
    require(std::string_view(tr("bcm_tyreLFPressure")) == "bcm_tyreLFPressure");
    require(std::string_view(tr("О программе")) == "关于");
    require(std::string_view(tr("Завершить Voyemu")) == "退出 Voyemu");
    require(std::string_view(tr("Двери")) == "车门");
    require(std::string_view(tr("Передняя левая")) == "左前");
    require(std::string_view(tr("Задняя правая")) == "右后");
    require(std::string_view(tr("Все")) == "全部");
    require(language_choices[0] == Language::chinese && language_choices[1] == Language::english &&
            language_choices[2] == Language::russian);
    require(std::string_view(language_choice_label(Language::chinese)) == "中文" &&
            std::string_view(language_choice_label(Language::english)) == "EN" &&
            std::string_view(language_choice_label(Language::russian)) == "RU");
    std::cout << "UI localization invariants passed\n";
} catch (const std::exception &error) {
    std::cerr << error.what() << '\n';

    return 1;
}
