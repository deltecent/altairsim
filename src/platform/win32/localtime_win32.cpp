#include "platform/localtime.h"

namespace altair::platform {

CalendarTime localCalendar(std::time_t t) {
    std::tm tmv{};
    localtime_s(&tmv, &t);  // MSVC: reentrant, and no C4996 -- note the reversed args
    return CalendarTime{tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday,
                        tmv.tm_hour,        tmv.tm_min,     tmv.tm_sec};
}

}  // namespace altair::platform
