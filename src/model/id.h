#pragma once

#include <string>
#include <string_view>

namespace ytp {

using Id = std::string;

[[nodiscard]] Id createId();
[[nodiscard]] bool isValidId(std::string_view value) noexcept;

} // namespace ytp

