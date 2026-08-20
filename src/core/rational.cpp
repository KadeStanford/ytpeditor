#include "core/rational.h"

#include <limits>
#include <numeric>
#include <stdexcept>

namespace ytp {
namespace {

std::int64_t checkedAdd(const std::int64_t left, const std::int64_t right) {
    if ((right > 0 && left > std::numeric_limits<std::int64_t>::max() - right) ||
        (right < 0 && left < std::numeric_limits<std::int64_t>::min() - right)) {
        throw std::overflow_error("rational addition overflow");
    }
    return left + right;
}

std::int64_t checkedMultiply(const std::int64_t left, const std::int64_t right) {
    if (left == 0 || right == 0) {
        return 0;
    }
    const auto minimum = std::numeric_limits<std::int64_t>::min();
    const auto maximum = std::numeric_limits<std::int64_t>::max();
    bool overflow = false;
    if (left > 0) {
        overflow = right > 0 ? left > maximum / right : right < minimum / left;
    } else {
        overflow = right > 0 ? left < minimum / right : left < maximum / right;
    }
    if (overflow) {
        throw std::overflow_error("rational multiplication overflow");
    }
    return left * right;
}

} // namespace

Rational::Rational(std::int64_t numerator, std::int64_t denominator) {
    if (denominator == 0) {
        throw std::invalid_argument("rational denominator cannot be zero");
    }
    if (denominator < 0) {
        if (denominator == std::numeric_limits<std::int64_t>::min() ||
            numerator == std::numeric_limits<std::int64_t>::min()) {
            throw std::overflow_error("rational normalization overflow");
        }
        numerator = -numerator;
        denominator = -denominator;
    }
    const auto divisor = std::gcd(numerator, denominator);
    numerator_ = numerator / divisor;
    denominator_ = denominator / divisor;
}

long double Rational::asLongDouble() const noexcept {
    return static_cast<long double>(numerator_) / static_cast<long double>(denominator_);
}

std::string Rational::toString() const {
    return std::to_string(numerator_) + "/" + std::to_string(denominator_);
}

Rational Rational::operator-() const {
    if (numerator_ == std::numeric_limits<std::int64_t>::min()) {
        throw std::overflow_error("rational negation overflow");
    }
    return {-numerator_, denominator_};
}

Rational operator+(const Rational& left, const Rational& right) {
    const auto divisor = std::gcd(left.denominator_, right.denominator_);
    const auto leftScale = right.denominator_ / divisor;
    const auto rightScale = left.denominator_ / divisor;
    const auto numerator = checkedAdd(checkedMultiply(left.numerator_, leftScale),
                                      checkedMultiply(right.numerator_, rightScale));
    return {numerator, checkedMultiply(left.denominator_, leftScale)};
}

Rational operator-(const Rational& left, const Rational& right) {
    return left + (-right);
}

Rational operator*(const Rational& left, const Rational& right) {
    const auto leftCancel = std::gcd(left.numerator_, right.denominator_);
    const auto rightCancel = std::gcd(right.numerator_, left.denominator_);
    return {checkedMultiply(left.numerator_ / leftCancel, right.numerator_ / rightCancel),
            checkedMultiply(left.denominator_ / rightCancel, right.denominator_ / leftCancel)};
}

Rational operator/(const Rational& left, const Rational& right) {
    if (right.numerator_ == 0) {
        throw std::invalid_argument("cannot divide by zero");
    }
    return left * Rational{right.denominator_, right.numerator_};
}

bool operator<(const Rational& left, const Rational& right) {
    const auto divisor = std::gcd(left.denominator_, right.denominator_);
    return checkedMultiply(left.numerator_, right.denominator_ / divisor) <
           checkedMultiply(right.numerator_, left.denominator_ / divisor);
}

Rational frameDuration(const std::int64_t rateNumerator,
                       const std::int64_t rateDenominator) {
    if (rateNumerator <= 0 || rateDenominator <= 0) {
        throw std::invalid_argument("frame rate must be positive");
    }
    return {rateDenominator, rateNumerator};
}

Rational timeAtFrame(const std::int64_t frame,
                     const std::int64_t rateNumerator,
                     const std::int64_t rateDenominator) {
    return Rational{frame, 1} * frameDuration(rateNumerator, rateDenominator);
}

} // namespace ytp
