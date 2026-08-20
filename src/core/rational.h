#pragma once

#include <cstdint>
#include <string>

namespace ytp {

class Rational final {
public:
    Rational() = default;
    Rational(std::int64_t numerator, std::int64_t denominator);

    [[nodiscard]] std::int64_t numerator() const noexcept { return numerator_; }
    [[nodiscard]] std::int64_t denominator() const noexcept { return denominator_; }
    [[nodiscard]] long double asLongDouble() const noexcept;
    [[nodiscard]] std::string toString() const;

    [[nodiscard]] Rational operator-() const;

    friend Rational operator+(const Rational& left, const Rational& right);
    friend Rational operator-(const Rational& left, const Rational& right);
    friend Rational operator*(const Rational& left, const Rational& right);
    friend Rational operator/(const Rational& left, const Rational& right);
    Rational& operator+=(const Rational& right) { *this = *this + right; return *this; }
    Rational& operator-=(const Rational& right) { *this = *this - right; return *this; }

    friend bool operator==(const Rational&, const Rational&) = default;
    friend bool operator<(const Rational& left, const Rational& right);
    friend bool operator>(const Rational& left, const Rational& right) { return right < left; }
    friend bool operator<=(const Rational& left, const Rational& right) { return !(right < left); }
    friend bool operator>=(const Rational& left, const Rational& right) { return !(left < right); }

private:
    std::int64_t numerator_{0};
    std::int64_t denominator_{1};
};

// Exact duration of one frame for rates such as 30000/1001.
[[nodiscard]] Rational frameDuration(std::int64_t rateNumerator,
                                     std::int64_t rateDenominator = 1);

[[nodiscard]] Rational timeAtFrame(std::int64_t frame,
                                   std::int64_t rateNumerator,
                                   std::int64_t rateDenominator = 1);

} // namespace ytp
