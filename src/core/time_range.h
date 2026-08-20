#pragma once

#include "core/rational.h"

namespace ytp {

class TimeRange final {
public:
    TimeRange(Rational start, Rational duration);

    [[nodiscard]] const Rational& start() const noexcept { return start_; }
    [[nodiscard]] const Rational& duration() const noexcept { return duration_; }
    [[nodiscard]] Rational end() const { return start_ + duration_; }
    [[nodiscard]] bool isEmpty() const noexcept { return duration_ == Rational{}; }
    [[nodiscard]] bool contains(const Rational& time) const;
    [[nodiscard]] bool intersects(const TimeRange& other) const;

    friend bool operator==(const TimeRange&, const TimeRange&) = default;

private:
    Rational start_;
    Rational duration_;
};

} // namespace ytp
