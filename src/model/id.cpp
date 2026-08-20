#include "model/id.h"

#include <array>
#include <cstdint>
#include <random>

namespace ytp {

Id createId() {
    std::array<std::uint8_t, 16> bytes{};
    std::random_device source;
    for (auto& byte : bytes) {
        byte = static_cast<std::uint8_t>(source());
    }
    bytes[6] = static_cast<std::uint8_t>((bytes[6] & 0x0fU) | 0x40U);
    bytes[8] = static_cast<std::uint8_t>((bytes[8] & 0x3fU) | 0x80U);

    constexpr char digits[] = "0123456789abcdef";
    std::string value;
    value.reserve(36);
    for (std::size_t index = 0; index < bytes.size(); ++index) {
        if (index == 4 || index == 6 || index == 8 || index == 10) {
            value.push_back('-');
        }
        value.push_back(digits[bytes[index] >> 4U]);
        value.push_back(digits[bytes[index] & 0x0fU]);
    }
    return value;
}

bool isValidId(const std::string_view value) noexcept {
    if (value.size() != 36) {
        return false;
    }
    for (std::size_t index = 0; index < value.size(); ++index) {
        if (index == 8 || index == 13 || index == 18 || index == 23) {
            if (value[index] != '-') {
                return false;
            }
            continue;
        }
        const char character = value[index];
        const bool isDigit = character >= '0' && character <= '9';
        const bool isLowerHex = character >= 'a' && character <= 'f';
        if (!isDigit && !isLowerHex) {
            return false;
        }
    }
    return true;
}

} // namespace ytp

