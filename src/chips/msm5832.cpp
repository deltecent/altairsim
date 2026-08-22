#include "chips/msm5832.h"

#include "core/statefile.h"

#include <cstdio>

namespace altair {

namespace {
constexpr uint8_t kHold = 0x40;   // command bit 6
constexpr uint8_t kWrite = 0x20;  // command bit 5
// bit 4 (Read) is not needed: a data-register read always returns the selected
// digit, which is what every real read sequence relies on.
constexpr uint8_t kDigitSel = 0x0F;  // command bits 3-0

bool isLeap(int y) { return y % 4 == 0 && (y % 100 != 0 || y % 400 == 0); }
}  // namespace

// ---------------------------------------------------------------------------
// THE COMMAND PORT (Base+10). Latch the digit select, run a Write strobe if asked,
// and track the Hold edges that fence a set.
void Msm5832::writeCommand(uint8_t v) {
    digitSel_ = v & kDigitSel;
    bool newHold = (v & kHold) != 0;

    // A Write strobe commits the staged data byte into the selected digit. The
    // seconds digits are not settable -- any write to them is forced to 0.
    if (v & kWrite) {
        ensureEdit();
        if (digitSel_ < 13) {
            uint8_t d = dataLatch_ & 0x0F;
            if (digitSel_ == 0 || digitSel_ == 1) d = 0;  // Seconds-1 / Seconds-10
            editDigits_[digitSel_] = d;
            dirty_ = true;
        }
    }

    if (newHold && !hold_) ensureEdit();  // rising edge: freeze the display to edit it
    if (!newHold && hold_) {              // falling edge: commit, but only if we wrote
        if (dirty_) applyEdit();
        editing_ = false;
        dirty_ = false;
    }
    hold_ = newHold;
}

void Msm5832::writeData(uint8_t v) { dataLatch_ = v & 0x0F; }

uint8_t Msm5832::readData() const {
    if (editing_) return digitSel_ < 13 ? editDigits_[digitSel_] : 0;
    platform::CalendarTime c = displayed();
    return digitSel_ < 13 ? digitOf(digitSel_, c) : 0;
}

// ---------------------------------------------------------------------------
// The nibble a digit reads as. Hours-10 and Days-10 carry a mode flag above their
// two digit bits (see the header); everything else is plain BCD in the low nibble.
uint8_t Msm5832::digitOf(int sel, const platform::CalendarTime& c) {
    int sec = c.second > 59 ? 59 : c.second;  // clamp a possible leap second
    switch (sel) {
        case 0:  return (uint8_t)(sec % 10);
        case 1:  return (uint8_t)(sec / 10);
        case 2:  return (uint8_t)(c.minute % 10);
        case 3:  return (uint8_t)(c.minute / 10);
        case 4:  return (uint8_t)(c.hour % 10);
        case 5:  return (uint8_t)((c.hour / 10) | 0x08);  // 24-hour mode bit set
        case 6:  return (uint8_t)c.weekday;               // 0-6
        case 7:  return (uint8_t)(c.day % 10);
        case 8:  return (uint8_t)((c.day / 10) | (isLeap(c.year) ? 0x04 : 0));
        case 9:  return (uint8_t)(c.month % 10);
        case 10: return (uint8_t)(c.month / 10);
        case 11: return (uint8_t)(c.year % 10);
        case 12: return (uint8_t)((c.year / 10) % 10);
        default: return 0;
    }
}

void Msm5832::ensureEdit() {
    if (editing_) return;
    platform::CalendarTime c = displayed();
    for (int i = 0; i < 13; ++i) editDigits_[i] = digitOf(i, c);
    editing_ = true;
}

// Compose the edited digits back into a host time_t and remember the delta. The
// weekday digit is not consumed: mktime derives the day of week from the date, so a
// weekday the guest set inconsistent with the date is ignored rather than believed.
void Msm5832::applyEdit() {
    std::tm tm{};
    tm.tm_sec = 0;  // setting the clock always zeroes seconds (they are write-ignored)
    tm.tm_min = (editDigits_[3] & 0x0F) * 10 + (editDigits_[2] & 0x0F);
    tm.tm_hour = (editDigits_[5] & 0x03) * 10 + (editDigits_[4] & 0x0F);
    tm.tm_mday = (editDigits_[8] & 0x03) * 10 + (editDigits_[7] & 0x0F);
    tm.tm_mon = ((editDigits_[10] & 0x0F) * 10 + (editDigits_[9] & 0x0F)) - 1;

    // The chip holds a two-digit year; pin the century from the host's current year.
    int yy = (editDigits_[12] & 0x0F) * 10 + (editDigits_[11] & 0x0F);
    platform::CalendarTime host = platform::localCalendar(std::time(nullptr));
    tm.tm_year = ((host.year / 100) * 100 + yy) - 1900;
    tm.tm_isdst = -1;  // let mktime resolve DST for the composed local time

    std::time_t target = std::mktime(&tm);
    if (target == (std::time_t)-1) return;  // unrepresentable date -- ignore the set
    offset_ = (long long)target - (long long)std::time(nullptr);
}

void Msm5832::reset() {
    // offset_ SURVIVES: the chip is battery-backed and keeps time across RESET.
    digitSel_ = 0;
    dataLatch_ = 0;
    hold_ = false;
    editing_ = false;
    dirty_ = false;
}

void Msm5832::serialize(StateWriter& w) const {
    w.u64((uint64_t)(int64_t)offset_);
    w.u8(digitSel_);
    w.u8(dataLatch_);
    w.boolean(hold_);
    w.boolean(editing_);
    w.boolean(dirty_);
    w.raw(editDigits_, 13);
}

void Msm5832::deserialize(StateReader& r) {
    offset_ = (long long)(int64_t)r.u64();
    digitSel_ = r.u8();
    dataLatch_ = r.u8();
    hold_ = r.boolean();
    editing_ = r.boolean();
    dirty_ = r.boolean();
    r.raw(editDigits_, 13);
}

platform::CalendarTime Msm5832::displayed() const {
    return platform::localCalendar((std::time_t)((long long)std::time(nullptr) + offset_));
}

std::string Msm5832::describe() const {
    platform::CalendarTime c = displayed();
    char buf[64];
    std::snprintf(buf, sizeof buf, "%04d-%02d-%02d %02d:%02d:%02d (host%+lds)", c.year,
                  c.month, c.day, c.hour, c.minute, c.second, (long)offset_);
    return buf;
}

}  // namespace altair
