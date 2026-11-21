#pragma once
#include <ctime>
namespace voyemu::os {
// The calendar fields of time in the local time zone of the host.
std::tm local_calendar(std::time_t time);
// The calendar fields of time in UTC.
std::tm utc_calendar(std::time_t time);
} // namespace voyemu::os
