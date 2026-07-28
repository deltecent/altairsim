#include "platform/localtime.h"

namespace altair::platform {

CalendarTime localCalendar(std::time_t t) {
    std::tm tmv{};
    localtime_r(&t, &tmv);  // POSIX: reentrant, fills the caller's tm
    return CalendarTime{tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday,
                        tmv.tm_hour,        tmv.tm_min,     tmv.tm_sec};
}

}  // namespace altair::platform
