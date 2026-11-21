#include "os/clock.hpp"
namespace voyemu::os {
std::tm local_calendar(std::time_t time) {
    std::tm result{};

    localtime_r(&time, &result);

    return result;
}

std::tm utc_calendar(std::time_t time) {
    std::tm result{};

    gmtime_r(&time, &result);

    return result;
}
} // namespace voyemu::os
