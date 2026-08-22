#pragma once

// Broken-down LOCAL time, done thread-safely (DESIGN.md 2.1: the interface carries no
// OS type, and there is one implementation file per OS).
//
// This exists because plain std::localtime() writes into a shared static buffer -- a
// data race the instant two threads ask the clock -- and the reentrant spelling is the
// ONE thing that differs between hosts: POSIX has localtime_r(&t, &tm), MSVC has
// localtime_s(&tm, &t) (and treats the plain call as the deprecation error C4996 that
// this project treats as a build failure). That single difference is exactly what the
// platform layer is for: a signature with no OS type, and one small file per OS behind it.
//
// The Host Bridge's HDIR listing formats a host file's mtime with this; there is no
// timezone database dependency, just the host's own idea of local time.

#include <ctime>

namespace altair::platform {

// A calendar date and time in the HOST's local zone. Fields are the human ones, already
// unbiased: `year` is the full year (2026, not 126) and `month` is 1-12 (not 0-11).
struct CalendarTime {
    int year;    // full year, e.g. 2026
    int month;   // 1-12
    int day;     // 1-31
    int hour;    // 0-23
    int minute;  // 0-59
    int second;  // 0-60 (a leap second is possible)
    int weekday; // 0-6, Sunday=0 (tm_wday) -- the OKI MSM5832 clock has a day-of-week
                 // register, and it is the one calendar field a bare Y/M/D loses.
};

// Break `t` (seconds since the epoch) into LOCAL calendar fields. Thread-safe.
CalendarTime localCalendar(std::time_t t);

}  // namespace altair::platform
