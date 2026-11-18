#pragma once
#include <array>
#include <string>
#include <string_view>
namespace voyemu::ui {
enum class Language {
    english,
    russian,
    chinese
};
// UI-thread only. Protocol identifiers and canonical values never pass through this catalogue.
inline Language current_language = Language::english;

constexpr Language language_from_tag(std::string_view tag) {
    if (tag == "zh" || tag.starts_with("zh-") || tag.starts_with("zh_")) {
        return Language::chinese;
    }

    return tag == "ru" || tag.starts_with("ru-") || tag.starts_with("ru_") ? Language::russian
                                                                           : Language::english;
}

constexpr const char *language_tag(Language language) {
    return language == Language::russian ? "ru" : language == Language::chinese ? "zh" : "en";
}

struct Translation {
    const char *russian;
    const char *english;
    const char *chinese;
};
inline constexpr Translation catalogue[] = {
        {"Передняя левая дверь", "Front left door", "左前车门"},
        {"Передняя правая дверь", "Front right door", "右前车门"},
        {"Задняя левая дверь", "Rear left door", "左后车门"},
        {"Задняя правая дверь", "Rear right door", "右后车门"},
        {"Левый поворотник", "Left indicator", "左转向灯"},
        {"Правый поворотник", "Right indicator", "右转向灯"},
        {"Багажник", "Boot", "后备厢"},
        {"Ближний", "Low beam", "近光灯"},
        {"Введите значение", "Enter a value", "请输入数值"},
        {"Все 579", "All 579", "全部 579"},
        {"Все", "All", "全部"},
        {"Габариты", "Position lamps", "示廓灯"},
        {"Аварийка", "Hazard lights", "危险警示灯"},
        {"Давление", "Pressure", "胎压"},
        {"Дальний", "High beam", "远光灯"},
        {"Дверь ЗЛ", "Rear left door", "左后车门"},
        {"Дверь ЗП", "Rear right door", "右后车门"},
        {"Дверь ПЛ", "Front left door", "左前车门"},
        {"Дверь ПП", "Front right door", "右前车门"},
        {"Движение", "Driving", "行驶"},
        {"Двери", "Doors", "车门"},
        {"Левые", "Left", "左侧"},
        {"Правые", "Right", "右侧"},
        {"Передняя", "Front", "前门"},
        {"Задняя", "Rear", "后门"},
        {"ЛП", "FL", "左前"},
        {"ПП", "FR", "右前"},
        {"ЛЗ", "RL", "左后"},
        {"ПЗ", "RR", "右后"},
        {"Поворотник", "Indicator", "转向灯"},
        {"Левый", "Left", "左"},
        {"Правый", "Right", "右"},
        {"Пробег", "Mileage", "里程"},
        {"Диапазон или шаг", "Invalid range or step", "范围或步长无效"},
        {"Дневные огни", "Daytime lights", "日间行车灯"},
        {"Задняя левая", "Rear left", "左后"},
        {"Задняя правая", "Rear right", "右后"},
        {"Запас EV", "EV range", "纯电续航"},
        {"Запас ДВС", "Fuel range", "燃油续航"},
        {"Заряд", "Charge", "电量"},
        {"Значение", "Value", "数值"},
        {"Капот", "Bonnet", "发动机盖"},
        {"Кузов", "Body", "车身"},
        {"Лимит", "Limit", "限制"},
        {"Мощность", "Power", "功率"},
        {"Неизвестное значение: %llu", "Unknown value: %llu", "未知数值：%llu"},
        {"Нет соединения", "Disconnected", "未连接"},
        {"Ограничение профиля", "Profile limit", "车型限制"},
        {"Передача", "Gear", "挡位"},
        {"Передняя левая", "Front left", "左前"},
        {"Передняя правая", "Front right", "右前"},
        {"Поворот лев.", "Left indicator", "左转向灯"},
        {"Поворот прав.", "Right indicator", "右转向灯"},
        {"Поиск", "Search", "搜索"},
        {"Пробег, км", "Mileage, km", "里程，公里"},
        {"Свет", "Lights", "灯光"},
        {"Сигнал", "Signal", "信号"},
        {"Скорость", "Speed", "车速"},
        {"Снаружи", "Outside", "车外温度"},
        {"Температура", "Temperature", "温度"},
        {"Топливо", "Fuel", "燃油"},
        {"Туман сзади", "Rear fog", "后雾灯"},
        {"Шины", "Tyres", "轮胎"},
        {"бар", "bar", "bar"},
        {"км", "km", "km"},
        {"км/ч", "km/h", "km/h"},
        {"О программе", "About", "关于"},
        {"Завершить Voyemu", "Quit Voyemu", "退出 Voyemu"},
        {"Лицензии", "Licenses", "许可证"},
        {"Сайт", "Website", "网站"},
        {"Задний экран", "Rear screen", "后排屏"},
        {"Экраны", "Screens", "屏幕"},
        {"HUD", "HUD", "抬头显示"},
};

inline const char *tr(const char *russian) {
    if (current_language == Language::russian) {
        return russian;
    }

    for (const auto &entry : catalogue) {
        if (std::string_view(entry.russian) == russian) {
            return current_language == Language::chinese ? entry.chinese : entry.english;
        }
    }

    return russian;
}

// The release line of About for the ISO date YYYY-MM-DD of voyemu::release_date().
inline std::string release_text(std::string_view date) {
    static constexpr std::array<const char *, 12> russian{
            "января", "февраля", "марта",    "апреля",  "мая",    "июня",
            "июля",   "августа", "сентября", "октября", "ноября", "декабря"};
    static constexpr std::array<const char *, 12> english{
            "January", "February", "March",     "April",   "May",      "June",
            "July",    "August",   "September", "October", "November", "December"};
    const std::string year(date.substr(0, 4));
    const int month = (date[5] - '0') * 10 + (date[6] - '0');
    const auto day = std::to_string((date[8] - '0') * 10 + (date[9] - '0'));

    if (current_language == Language::russian) {
        return "Выпущена " + day + " " + russian.at(month - 1) + " " + year;
    }

    if (current_language == Language::chinese) {
        return "发布于" + year + "年" + std::to_string(month) + "月" + day + "日";
    }

    return "Released " + day + " " + english.at(month - 1) + " " + year;
}

// Language choice next to the wheel, in the vehicle's order.
inline constexpr std::array<Language, 3> language_choices{Language::chinese, Language::english,
                                                          Language::russian};

constexpr const char *language_choice_label(Language language) {
    return language == Language::russian ? "RU" : language == Language::chinese ? "中文" : "EN";
}
} // namespace voyemu::ui
