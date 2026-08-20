#include "core/time_range.h"

#include <stdexcept>

namespace ytp {

TimeRange::TimeRange(Rational start, Rational duration)
    : start_(start), duration_(duration) {
    if (duration_ < Rational{}) {
        throw std::invalid_argument("time range duration cannot be negative");
    }
}

bool TimeRange::contains(const Rational& time) const {
    return time >= start_ && time < end();
}

bool TimeRange::intersects(const TimeRange& other) const {
    return !isEmpty() && !other.isEmpty() && start_ < other.end() && other.start_ < end();
}

} // namespace ytp

